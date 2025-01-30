#!/usr/bin/env python3

import os
import ssl
import wave
import json
import asyncio
import argparse
import requests
import websockets
import numpy as np

import auto_uploader as auto_upl

WSS_API_URL="wss://app.banafo.ai/api/v1/transcripts/pre-recorded"
HTTP_API_URL = "https://app.banafo.ai"
HTTP_UPLOAD_API_URL = HTTP_API_URL + "/api/v1/file"
HTTP_GET_API_URL = HTTP_API_URL + "/api/v1/transcripts"

def generate_post_data(_wf_name,_lang):
	file_name = os.path.basename(_wf_name)
	if _lang is not None:
		data = {"fileName": file_name,"languageCode": _lang}
	else:
		data = {"fileName": file_name}

	return json.dumps(data)

def get_result_only_text(res):
	_json = json.loads(res)
	return _json['text']

def banafo_read_wav(filename):
	with wave.open(filename) as f:
		assert f.getnchannels() == 1, f.getnchannels()
		assert f.getsampwidth() == 2, f.getsampwidth()
		frames = f.readframes(f.getnframes())
		samples = np.frombuffer(frames, dtype=np.int16).astype(np.float32) / 32768
		return samples, f.getframerate()

async def banafo_audio_stream_to_ws(uri, wf_name, wflag=False, max_retries=5, retry_delay=10):
	_uri = uri.split(":");

	if 'wss' in _uri[0]:
		ssl_context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
		ssl_context.check_hostname = False
		ssl_context.verify_mode = ssl.CERT_NONE
	else:
		ssl_context = None

	for attempt in range(1, max_retries + 1):
		try:
			async with websockets.connect(uri, ssl=ssl_context) as websocket:
				if wflag:
					rr = await websocket.recv()
					auto_upl.logger.info(f"receive: {rr}")

				samples, rate = banafo_read_wav(wf_name)
				buf = rate.to_bytes(4, byteorder="little") + (samples.size * 4).to_bytes(4, byteorder="little") + samples.tobytes()

				for i in range(0, len(buf), 1_000_000):
					await websocket.send(buf[i:i + 1_000_000])
	
				resp = await websocket.recv()
				await websocket.send("Done")
				return get_result_only_text(resp)
		except Exception as e:
			auto_upl.logger.error(f"Attempt {attempt} failed with error: {str(e)}", exc_info=True)
			if attempt < max_retries:
				auto_upl.logger.info(f"Retrying in {retry_delay} seconds...")
				await asyncio.sleep(retry_delay)
			else:
				auto_upl.logger.error("Max retries reached. Could not establish connection.")
				return None

async def banafo_api_upload_to_wss(_apiKey,_lang,_wf_name):
	if _lang is None or (len(_lang) == 0):
		_lang = "en-US"

	uri = WSS_API_URL+"?apiKey="+str(_apiKey)+"&languageCode="+str(_lang)
	auto_upl.logger.debug(f"composed uri: {uri}")

	return await banafo_audio_stream_to_ws(uri, _wf_name, True)

async def banafo_api_upload_to_http(_apikey, _lang, _wf_name, max_retries=5, retry_delay=10):
	post_headers = {
		"accept": "application/json",
		"x-api-key": _apikey,
		"content-Type": "application/json"
	}
	post_data = generate_post_data(_wf_name,_lang)

	for attempt in range(1, max_retries + 1):
		try:
			response = requests.post(HTTP_UPLOAD_API_URL, headers=post_headers, data=post_data)

			if response.status_code >= 200 and response.status_code < 300:
				auto_upl.logger.info("Request POST was successful")
				break
			else:
				auto_upl.logger.error(f"Request POST failed with status code: {response.status_code}, text: {response.text}")
				return None
		except Exception as e:
			auto_upl.logger.error(f"Attempt {attempt} for POST failed with error: {str(e)}", exc_info=True)
			if attempt == max_retries:
				return None

		auto_upl.logger.info(f"Retrying POST in {retry_delay} seconds...")
		await asyncio.sleep(retry_delay)

	response_data = response.json()
	file_id = response_data.get("fileId", "")
	print (file_id)
	puturl = f"{HTTP_UPLOAD_API_URL}/{file_id}"

	put_headers = {
		"accept": "application/json",
		"x-api-key": _apikey,
		"x-start-byte": "0",
	}
	files = {"theFile": (os.path.basename(_wf_name), open(_wf_name, "rb"), "audio/x-wav")}

	for attempt in range(1, max_retries + 1):
		try:
			response = requests.put(puturl,headers = put_headers,files = files)

			if response.status_code == 200:
				auto_upl.logger.info("Request PUT was successful")
				return file_id
			else:
				auto_upl.logger.error(f"Request PUT failed with status code: {response.status_code}, text: {response.text}")
				return None
		except Exception as e:
			auto_upl.logger.error(f"Attempt {attempt} for PUT failed with error: {str(e)}", exc_info=True)
			if attempt == max_retries:
				return None

		auto_upl.logger.info(f"Retrying PUT in {retry_delay} seconds...")
		await asyncio.sleep(retry_delay)

	return None

def banafo_api_result(api_key, file_id):
	url = f"{HTTP_GET_API_URL}/{file_id}"

	headers = {
		"accept": "text/plain",
		"x-api-key": api_key,
	}

	response = requests.get(url, headers=headers)
	if response.status_code == 200:
		auto_upl.logger.info("Request GET was successful")
		return response.text
	else:
		auto_upl.logger.error(f"Request GET failed with status code: {response.status_code}, text: {response.text}")
		return None

def get_args():
	parser = argparse.ArgumentParser(
		formatter_class=argparse.ArgumentDefaultsHelpFormatter,
		add_help=True,
		description="'banafo_client.py' is command line tool",
		usage="./banafo_client.py -x command option1 option2 ... optionN"
	)

	parser.add_argument(
		"-x",
		"--command",
		type=str,
		default="help",
		help="Choise option: offline, api-wss, api-http, get",
	)

	parser.add_argument(
		"-f",
		"--file-name",
		type=str,
		default="",
		help="path to the audio file",
	)

	parser.add_argument(
		"-u",
		"--uri",
		type=str,
		default="",
		help="Address of the Banafo ASR server",
	)

	parser.add_argument(
		"-a",
		"--api",
		type=str,
		default="",
		help="apiKey from https://banafo.ai",
	)

	parser.add_argument(
		"-l",
		"--lang",
		type=str,
		default="en-US",
		help="Choise language in format 'en-US' for example",
	)

	parser.add_argument(
		"-i",
		"--fid",
		type=str,
		default="",
		help="",
	)

	return parser.parse_args()

def main():
	args = get_args()

	mode = args.command
	file_name = args.file_name or None
	uri = args.uri or None
	api = args.api or None
	fid = args.fid or None
	lang = args.lang

	if 'offline' in mode:
		if uri is not None and file_name is not None:
			print (uri)
			print (asyncio.run(banafo_audio_stream_to_ws(uri, file_name)))

	elif 'api-wss' in mode:
		if api is not None and file_name is not None:
			result = asyncio.run(banafo_api_upload_to_wss(api, lang, file_name))
			print ("result:\n", result)

	elif 'api-http' in mode:
		if api is not None and file_name is not None:
			file_id = asyncio.run(banafo_api_upload_to_http(api, lang, file_name))
			print ("file_id: ", file_id)

	elif 'get' in mode:
		if api is not None and fid is not None:
			result = banafo_api_result(api, fid)
			print (result)

if __name__ == "__main__":
	main()
