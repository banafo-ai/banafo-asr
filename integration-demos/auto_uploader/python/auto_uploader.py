#!/usr/bin/env python3

import os
import re
import sys
import time
import asyncio
import argparse
import datetime
from pydub import AudioSegment
from tabulate import tabulate

import logging
from enum import Enum
from logging.handlers import RotatingFileHandler

import auto_uploader_db as db
import banafo_client as bc

MP3_FILE_EXTEN = 'mp3'
WAV_FILE_EXTEN = 'wav'
GSM_FILE_EXTEN = 'gsm'

MAX_AUDIO_DURATION = 10
MAX_FILE_SIZE = 524288000

TXT_DIR = "./txt/"

MAX_TASKS = 5
MAX_QUEUE_SIZE = 10000

LOG_DIR = './logs/'
LOG_FILE_MSIZE = 40
LOG_FILE_COUNTER = 10

loop = True
supported_langs = ['en-US','bg-BG']

logger = logging.getLogger('auto_uploader')

class request_data:
	def __init__(self,fn,id,txt):
		self.fn = fn
		self.id = id
		self.txt = txt

	def put_api_key (self, a, l, f):
		self.api_key = a
		self.asr_lang = l
		self.http_flag = f

	def put_async_data(self, pr_f):
		self.processed_files = pr_f

class LogHandlerType(Enum):
	CUSTOM  = 'custom'
	SYSLOG  = 'syslog'
	CONSOLE = 'console'
	ALL     = 'all'

def _help():
	print("  Banafo auto uploader")
	print("\tUsage:")
	print("\t\t* insert a new path =>")
	print("\t\t\t./auto_uploader.py -x insert --path PATH --lang LANG --api apiKey --uri URI --txt DIR --res-interval RES_INTERVAL --res-attempts RES_ATTEMPTS\n")
	print("\t\t* list paths => ")
	print("\t\t\t./auto_uploader.py -x list\n")
	print("\t\t* remove path from the table =>")
	print("\t\t\t./auto_uploader.py -x remove  --id ID\n")
	print("\t\t* list waiting files from the file table =>")
	print("\t\t\t./auto_uploader.py -x pending\n")
	print("\t\t* list successfully uploaded files in the file table =>")
	print("\t\t\t./auto_uploader.py -x success\n")
	print("\t\t* list unsuccessfully (with some errors) uploaded files in the file table =>")
	print("\t\t\t./auto_uploader.py -x errors\n")
	print("\t\t* delete (flush) all files in the file table =>")
	print("\t\t\t./auto_uploader.py -x delete\n")
	print("\t\t* searching in inserted paths and upload if find audio files (.wav,.mp3,.gsm) =>")
	print("\t\t\t./auto_uploader.py -x upload\n")
	print("\t\t* get results of the uploaded to the Banafo site files and save in TXT files =>")
	print("\t\t\t./auto_uploader.py -x get\n")
	print()

def auto_upl_logging_setup(handler_type, log, level, max_file_size=40, backup_count=10):
	logger = logging.getLogger('auto_uploader')
	logger.propagate = False

	# Define the logging level
	level_dict = {
        'INFO':     logging.INFO,
        'WARNING':  logging.WARNING,
        'ERROR':    logging.ERROR,
        'DEBUG':    logging.DEBUG,
        'CRITICAL': logging.CRITICAL,
    }
	log_level = level_dict.get(level.upper(), logging.INFO)
	logger.setLevel(log_level)

	formatter = logging.Formatter('[%(levelname)s],[%(asctime)s],[%(funcName)s():%(lineno)d],%(message)s')

	if handler_type in (LogHandlerType.ALL, LogHandlerType.CUSTOM):
		if not log:
			os.makedirs(LOG_DIR, exist_ok=True)
			log_file = os.path.join(LOG_DIR, 'auto_uploader.log')
		else:
			log_file = log

		file_handler = RotatingFileHandler(log_file, (max_file_size*1024*1024), backup_count)
		file_handler.setLevel(log_level)
		file_handler.setFormatter(formatter)

		logger.addHandler(file_handler)

	if handler_type in (LogHandlerType.ALL, LogHandlerType.CONSOLE):
		console_handler = logging.StreamHandler()
		console_handler.setLevel(log_level)
		console_handler.setFormatter(formatter)

		logger.addHandler(console_handler)

	if handler_type in (LogHandlerType.ALL, LogHandlerType.SYSLOG):
		try:
			# SysLogHandler: Forward to the system journal (syslog)
			syslog_handler = logging.handlers.SysLogHandler(address='/dev/log')
			#syslog_handler = logging.handlers.SysLogHandler(address=('localhost', 514)) 
			syslog_handler.setLevel(log_level)
			syslog_handler.setFormatter(formatter)
			logger.addHandler(syslog_handler)
		except Exception as e:
			print(f"SysLogHandler could not be added: {e}")

	return logger

def auto_upl_get_dur(file_path):
	try:
		audio_file = AudioSegment.from_file(file_path)
		duration = audio_file.duration_seconds
	except Exception as e:
		logger.error("AudioSegment error: %s", e)
		duration = None
	return duration

def auto_upl_check_lang(chk_lang):
	for i in supported_langs:
		if i == chk_lang:
			return True
	return False

def auto_upl_convert_to_wav(input_file, input_format, output_file):
	try:
		ts1 = time.time()
		audio = AudioSegment.from_file(input_file, input_format)
		audio.export(output_file, format="wav")
		ts2 = time.time()
		logger.info(f"Convert file [{input_format}]: {input_file}\nin {output_file}\ntime: {(ts2 - ts1)} sec")
		return True
	except Exception as e:
		logger.error(f"AudioSegment error: {e}")

	return False

def auto_upl_read_apikey_file(file_path):
    if not os.path.isfile(file_path):
        return None

    apiKey = None
    with open(file_path, 'r') as file:
        file_contents = file.read()
        apiKey = file_contents.rstrip('\n')

    return apiKey

def auto_upl_get_file_attr(file_path):
    attr = {}

    if os.path.isfile(file_path):
        file_stats = os.stat(file_path)
    else:
        return None

    attr['created'] = datetime.datetime.fromtimestamp(file_stats.st_ctime)
    attr['created_ts'] = file_stats.st_ctime
    attr['last_accessed'] = datetime.datetime.fromtimestamp(file_stats.st_atime)
    attr['last_modified'] = datetime.datetime.fromtimestamp(file_stats.st_mtime)
    attr['size'] = file_stats.st_size
    attr['permissions'] = oct(file_stats.st_mode)[-3:]

    return attr

def auto_upl_list_paths(conn, cursor):
	lst = db.sqlite3_uploader_get_paths(conn, cursor)
	print("\nA table with path&lang for searching and uploading of audio files:")
	print(tabulate(lst,headers=['id','path','lang','apiKey','uri','res_interval','res_attempts','Result TXT file path','http_flag'],tablefmt='fancy_grid'))
	print("\n")

def auto_upl_list_files(conn,cursor,flag,op='='):
	lst = db.sqlite3_uploader_upload_files(conn, cursor,flag,op)
	print("\nA table with uploaded files:")
	print(tabulate(lst,headers=['ID','FileName','Language','Created TS','Uploaded TS','Finished TS','Flag','Status/Error'],tablefmt='fancy_grid'))
	print("Rows: ",len(lst))
	print("\n")

def auto_upl_remove_files(conn,cursor):
	print(f"\nFlushed files table\n")
	db.sqlite3_uploader_delete_files(conn,cursor)

def auto_upl_remove_path(conn,cursor,_id):
	print(f"\nremoving path, id: {_id}\n")
	db.sqlite3_uploader_delete_path(conn,cursor,_id)

def auto_upl_file_validate(in_filename):
	out_filename = None

	if '.' in in_filename:
		base_filename, ext_filename = in_filename.rsplit('.', 1)
		if len(base_filename) == 0:
			return out_filename
	else:
		return out_filename

	if ext_filename == WAV_FILE_EXTEN:
		out_filename = in_filename
	elif ext_filename == MP3_FILE_EXTEN:
		out_filename = f"{base_filename}.{WAV_FILE_EXTEN}"
		if not os.path.isfile(out_filename):
			if auto_upl_convert_to_wav(in_filename, MP3_FILE_EXTEN, out_filename) is False:
				return None
	elif ext_filename == GSM_FILE_EXTEN:
		out_filename = f"{base_filename}.{WAV_FILE_EXTEN}"
		if not os.path.isfile(out_filename):
			if auto_upl_convert_to_wav(in_filename, GSM_FILE_EXTEN, out_filename) is False:
				return None

	return out_filename

def auto_upl_get_files_from_dir(directory):
	au_files = []
	for root, _, files in os.walk(directory):
		for file in files:
			_au_file = auto_upl_file_validate(os.path.join(root,file))

			if _au_file is not None:
				# full path to the validated audio file
				validated_file = os.path.join(root, _au_file)
				logger.info(f"File (step 1: find, validate & append in an array): {validated_file}")
				au_files.append(validated_file)

	return au_files

def auto_upl_insert_file(conn, cursor,fn,asr_lang,path_id):
	flag = 0

	# the file is inserted in the db before
	if db.sqlite3_uploader_check_file(conn, cursor,fn) > 0:
		return -7

	attr = auto_upl_get_file_attr(fn)
	dur = auto_upl_get_dur(fn)
	if dur is None:
		return -6

	logger.info(f"Check file & insert in the table): {fn}, size: {attr['size']}, duration: {dur}")

	# file size checking
	if attr['size'] >= MAX_FILE_SIZE:
		flag = -2

	# file durarion checking as audio
	if dur < MAX_AUDIO_DURATION:
		flag = -3

	db.sqlite3_uploader_db(conn,cursor,"",fn,asr_lang,path_id,flag)
	return flag

def auto_upl_scan_dir(conn, cursor, _dir, _asr_lang, path_id):
	au_files = auto_upl_get_files_from_dir(_dir)
	logger.info(f"Number of files found: {len(au_files)}")

	for file_path in au_files:
		#flag = 
		auto_upl_insert_file(conn, cursor, file_path, _asr_lang, path_id)

		#if flag < 0:
		#	logger.error("Error processing file: %s, Flag: %d", file_path, flag)

	logger.info("File scanning and insertion completed.")

async def auto_upl_wss_worker(conn, cursor, queue, res_path, processed_files=None):
	os.makedirs(res_path, exist_ok=True)

	while True:
		logger.debug(f"wss worker, wait to get data from the queue")

		task_data = await queue.get()

		if task_data is None:
			logger.debug("Break!")
			break
		else:
			file_name = task_data.fn
			id = task_data.id
			api_key = task_data.api_key
			asr_lang = task_data.asr_lang

		logger.debug(f"wss worker, get a file: '{file_name}'")

		result = await bc.banafo_api_upload_to_wss(api_key, asr_lang, file_name)
		logger.debug(f"a file '{file_name}',\nresult:\n{result}\n")
		if processed_files is not None:
			processed_files.discard(file_name)
		queue.task_done()

		if result is not None:
			txt_file_name = re.sub(r'\.\w+$','',os.path.basename(file_name))
			fn = os.path.join(res_path, f"{txt_file_name}.txt")
			with open(fn, "w") as file:
				file.write(result)
				db.sqlite3_uploader_set_flag(conn,cursor,id,"",5)
		else:
			db.sqlite3_uploader_set_flag(conn,cursor,id,"",-9)

async def auto_upl_http_worker(conn, cursor, queue, res_path, processed_files=None):
	os.makedirs(res_path, exist_ok=True)

	while True:
		task_data = await queue.get()

		if task_data is None:
			logger.debug("Break!")
			break
		else:
			file_name = task_data.fn
			id = task_data.id
			api_key = task_data.api_key
			asr_lang = task_data.asr_lang

		logger.debug(f"http worker, starting task, '{file_name}'")

		file_id = await bc.banafo_api_upload_to_http(api_key, asr_lang, file_name)
		logger.info(f"file_id: {file_id}")
		if processed_files is not None:
			processed_files.discard(file_name)
		queue.task_done()

		if file_id is not None:
			if id == 0:
				id = db.sqlite3_uploader_check_file(cursor, file_name)
			db.sqlite3_uploader_set_flag(conn, cursor, id, file_id)
		else:
			db.sqlite3_uploader_set_flag(conn, cursor, id, "",-9)

async def auto_upl_upload_files_async(conn, cursor, api_key, res_path, tasks, max_qsize, http_flag):
	queue = asyncio.Queue(maxsize=max_qsize)

	logger.debug(f"http_flag: {http_flag}")

	if http_flag == 1:
		workers = [asyncio.create_task(auto_upl_http_worker(conn, cursor, queue, res_path)) for _ in range(tasks)]
	else:
		workers = [asyncio.create_task(auto_upl_wss_worker(conn, cursor, queue, res_path)) for _ in range(tasks)]

	t_files = db.sqlite3_uploader_upload_files(conn, cursor,0)
	t_files.extend(db.sqlite3_uploader_upload_files(conn, cursor, -9, "<="))
	for el in t_files:
		_id, _wf_name, _asr_lang = el[:3]

		task_data = request_data(_wf_name, _id, res_path)
		task_data.put_api_key(api_key, _asr_lang, http_flag)

		logger.debug(f"id: {_id}, before queue.put")

		await queue.put(task_data)

	await queue.join()

	for _ in workers:
		await queue.put(None)

	await asyncio.gather(*workers)

def auto_upl_insert_path(conn,cursor,_dir,_lang,_api_key=None,_uri=None,_res_interval=0,_res_attempts=0,_res_path=None,_http_flag=0):
	_dir_  = os.path.abspath(_dir)
	if _dir_[-1] != '/':
		_dir_ = _dir_ + '/'

	if _lang == "None":
		_lang = None

	if _api_key == "None":
		_api_key = None

	if _uri == "None":
		_uri = None

	if os.path.exists(_dir_):
		#if auto_upl_check_lang(_lang):
			db.sqlite3_uploader_insert_path(conn,cursor,_dir_,_lang,_api_key,_uri,_res_interval,_res_attempts,_res_path,_http_flag)
		#else:
		#	print(f"\nA '{_lang}' is not supported!Choose from follow:\n{supported_langs}\n")
	else:
		logger.error(f"\nA '{_dir_}' doesn\'t exist!\nInsert correct dirname!\n")

def auto_upl_scan_and_upload(conn, cursor,task_nums,max_queue_size):
	paths = db.sqlite3_uploader_get_paths(conn, cursor)
	return auto_upl_scan_and_upload_async(conn, cursor, paths, task_nums, max_queue_size)

def auto_upl_scan_and_upload_async(conn, cursor, paths, tasks, max_qsize):
	if not paths:
		logger.error("\nYou don't have inserted paths in the DB! Insert first!\n")
		return None

	for el in paths:
		id, path, asr_lang, api_key, uri, _, _, res_path, http_flag = el

		logger.info(f"Processing path: {path} (ID: {id})")

		asr_lang = asr_lang or None
		api_key = api_key or None
		uri = uri or None
		res_path = res_path or TXT_DIR

		auto_upl_scan_dir(conn, cursor, path, asr_lang, id)

		if api_key:
			asyncio.run(auto_upl_upload_files_async(conn, cursor, api_key, res_path, tasks, max_qsize, http_flag))
		elif uri:
			asyncio.run(auto_upl_to_asr_server_async(conn, cursor, uri, res_path, tasks, max_qsize))
		else:
			return False

	return True

def auto_upl_get_result_files(conn, cursor, apiKey):
	paths_data = db.sqlite3_uploader_get_path_from_api(conn, cursor, apiKey)
	for row in paths_data:
		res_path = row[7]
		os.makedirs(res_path, exist_ok=True)

		files = db.sqlite3_uploader_get_file_status_eq(conn, cursor,1)
		for el in files:
			id = el[0]
			fn = re.sub(r'\.\w+$','',os.path.basename(el[1]))
			file_id = el[6]

			result = bc.banafo_api_result(apiKey,file_id)
			if result is not None:
				fn = res_path + fn + ".txt"
				with open(fn, "w") as file:
					file.write(result)
					db.sqlite3_uploader_set_flag(conn,cursor,id,file_id,4)

def auto_upl_get_result_files_v2(conn, cursor, path_id , apiKey, res_attempts, result_path):
	os.makedirs(result_path, exist_ok=True)

	files = db.sqlite3_uploader_get_file_status_path_id(conn, cursor,path_id,1)

	num_files = len(files)
	if num_files == 0:
		return num_files

	for file_info in files:
		file_id = file_info[0]
		file_path = file_info[1]
		result_file_id = file_info[6]
		current_res_attempts = file_info[7]

		file_name_without_ext = re.sub(r'\.\w+$', '', os.path.basename(file_path))

		result = bc.banafo_api_result(apiKey, result_file_id)
		if result is not None:
			result_file_path = os.path.join(result_path, f"{file_name_without_ext}.txt")
			with open(result_file_path, "w") as result_file:
				result_file.write(result)
				db.sqlite3_uploader_set_flag(conn, cursor, file_id, result_file_id, 4)
		else:
			if current_res_attempts < res_attempts:
				current_res_attempts += 1
				logger.info(f"A file: '{file_path}' doesn't have a result; current_res_attempt: {current_res_attempts};")
				db.sqlite3_uploader_set_res_attempts(conn, cursor, file_id, current_res_attempts)
			else:
				db.sqlite3_uploader_set_flag(conn, cursor, file_id, result_file_id, -8)

	return num_files

async def auto_upl_queue_async_producer(queue, filename, file_id , txt_res_path, api_key=None, asr_lang=None, http_flag=0):
	data = request_data(filename, file_id, txt_res_path)
	if api_key:
		data.put_api_key(api_key, asr_lang, http_flag)
	if queue.full():
		logger.critical("asyncio queue is FULL!")
		return False
	else:
		await queue.put(data)
		return True

async def auto_upl_asr_server_worker(conn, cursor, queue, uri, res_path, processed_files=None):
	while True:
		logger.debug(f"worker, wait to get data from the queue")

		task_data = await queue.get()

		if task_data is None:
			logger.debug("Break!")
			break
		else:
			file_name = task_data.fn
			id = task_data.id

		logger.debug(f"worker, get file: '{file_name}'")

		result = await bc.banafo_audio_stream_to_ws(uri, file_name)
		logger.debug(f"a file '{file_name}',\nresult:\n{result}\n")
		if processed_files is not None:
			processed_files.discard(file_name)
		queue.task_done()

		if result is not None:
			txt_file_name = re.sub(r'\.\w+$','',os.path.basename(file_name))
			fn = os.path.join(res_path, f"{txt_file_name}.txt")
			with open(fn, "w") as file:
				file.write(result)
				db.sqlite3_uploader_set_flag(conn,cursor,id,"",5)
		else:
			db.sqlite3_uploader_set_flag(conn,cursor,id,"",-9)

async def auto_upl_to_asr_server_async(conn, cursor, uri, res_path, threads, max_qsize):
	logger.info(f"Banafo ASR server (async): {uri}")
	logger.info(f"Result TXT files path: {res_path}")

	os.makedirs(res_path, exist_ok=True)

	queue = asyncio.Queue(maxsize=max_qsize)

	workers = [asyncio.create_task(auto_upl_asr_server_worker(conn, cursor, queue, uri, res_path)) for _ in range(threads)]

	files = db.sqlite3_uploader_get_file_status_or(conn, cursor, 0, -9)
	logger.info(f"files: {len(files)}")
	for el in files:
		id , path = el[:2]
		task_data = request_data(path, id, res_path)
		logger.debug(f"id: {id}, before queue.put")
		await queue.put(task_data)

	await queue.join()

	for _ in workers:
		await queue.put(None)
	await asyncio.gather(*workers)

	return True

def get_args():
    parser = argparse.ArgumentParser(
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
        add_help=True,
        description="'auto_uploader.py' is command line tool",
        usage="./auto_uploader.py -x command option1 option2 ... optionN"
    )

#    parser = argparse.ArgumentParser(
#        prog='PROG',
#        formatter_class=argparse.RawDescriptionHelpFormatter,
#        #description=textwrap.dedent(_help()))
#        description=_help())

    parser.add_argument(
        "-x",
        "--command",
        type=str,
        default="help",
        help="Choise option: insert,remove,list,files,errors,unkw,delete",
    )

    parser.add_argument(
        "--path",
        type=str,
        default="",
        help="path to the audio files",
    )

    parser.add_argument(
        "--uri",
        type=str,
        default="",
        help="Address of the Banafo ASR server",
    )

    parser.add_argument(
        "--api",
        type=str,
        default="",
        help="apiKey from https://banafo.ai",
    )

    parser.add_argument(
        "--txt",
        type=str,
        default=TXT_DIR,
        help="Directory for TXT files as results",
    )

    parser.add_argument(
        "--lang",
        type=str,
        default="",
        help="Choise language in format 'en-US' for example",
    )

    parser.add_argument(
        "--res-interval",
        type=int,
        default=0,
        help="get result interval",
    )

    parser.add_argument(
        "--res-attempts",
        type=int,
        default=0,
        help="get result attemps",
    )

    parser.add_argument(
        "--id",
        type=int,
        default=0,
        help="row (from sqlite3 table) id",
    )

    parser.add_argument(
        "--http",
        type=int,
        default=0,
        help="a 'http' flag activates sending to http Banafo API",
    )

    parser.add_argument(
        "--task-nums",
        type=int,
        default=MAX_TASKS,
        help="Tasks number in the Banafo ASR server mode",
    )

    parser.add_argument(
        "--max-queue-size",
        type=int,
        default=MAX_QUEUE_SIZE,
        help="MAX Queue size (max waiting audio files to uploading) in the Banafo ASR server mode",
    )

    parser.add_argument(
        "--log",
        type=str,
        default="",
        help="log : full path + filename",
    )

    parser.add_argument(
        "--log-type",
        type=str,
        default="syslog",
        help="log type: syslog, custom, console, all",
    )

    parser.add_argument(
        "--log-level",
        type=str,
        default="INFO",
        help="log level: 'INFO','DEBUG','WARNING','ERROR','CRITICAL'",
    )

    parser.add_argument(
        "--log-file-msize",
        type=int,
        default=LOG_FILE_MSIZE,
        help="log file max size: in MB",
    )

    parser.add_argument(
        "--log-file-counter",
        type=int,
        default=LOG_FILE_COUNTER,
        help="log file counter",
    )

    return parser.parse_args()

def main():
    args = get_args()
    curr_dir = os.getcwd()
    apiPath = curr_dir + "/apiKey.txt"
    if not os.path.isfile(apiPath):
        apiPath = "/usr/local/auto_uploader/apiKey.txt"

    apiKey = auto_upl_read_apikey_file(apiPath)

    if not os.path.exists(db.UPLOADER_DB_DIR):
        print("\nYou don't have a db file!\nYou can execute first './install.sh'\n")
        sys.exit(0)

    auto_upl_logging_setup(LogHandlerType(args.log_type), args.log, args.log_level, args.log_file_msize, args.log_file_counter)
    logger.debug(f"Parsed arguments: {args}")

    conn, cursor = db.sqlite3_conn(db.UPLOADER_DB)

    mode = args.command
    if 'insert' in mode:
        _path = args.path

        _lang = args.lang
        if _lang == "":
            _lang = None

        _api = args.api
        if _api == "":
            _api = None

        _uri = args.uri
        if _uri == "":
            _uri = None

        _res_interval = int(args.res_interval)
        _res_attempts = int(args.res_attempts)

        _res_path = args.txt
        if _res_path == "":
            _res_path = None

        _http_flag = int(args.http)

        auto_upl_insert_path(conn,cursor,_path,_lang,_api,_uri,_res_interval,_res_attempts,_res_path,_http_flag)
        auto_upl_list_paths(conn, cursor)
    elif 'list' in mode:
        auto_upl_list_paths(conn, cursor)
    elif 'pending' in mode:
        auto_upl_list_files(conn,cursor,0)
    elif 'success' in mode:
        auto_upl_list_files(conn,cursor,0,'>')
    elif 'errors' in mode:
        auto_upl_list_files(conn,cursor,0,'<')
    elif 'remove' in mode:
        if args.id > 0:
            auto_upl_remove_path(conn,cursor,args.id)
            auto_upl_list_paths(conn, cursor)
        else:
            print("\nEmpty path ID!\nPlease use 'remove <path_id>'\n")
    elif 'delete' in mode:
        auto_upl_remove_files(conn,cursor)
    elif 'upload' in mode:
        task_nums = args.task_nums
        max_queue_size = args.max_queue_size
        auto_upl_scan_and_upload(conn, cursor,task_nums,max_queue_size)
    elif 'get' in mode:
        if apiKey is None:
            if args.api != "":
                apiKey = args.api
            else:
                print("\nEmpty 'apiKey.txt'!\nYou have to create a file or to use '--api' option into this command!\n")
                return
        auto_upl_get_result_files(conn, cursor, apiKey)
    elif 'help' in mode:
        _help()
    else:
        print("\nIncorrect option!\n")
        _help()

    db.sqlite3_close(conn,cursor)

if __name__ == "__main__":
	try:
		s_timer = time.time()
		main()
		f_timer = time.time()
		print("execute time: ",round((f_timer-s_timer)*1000,2),"ms\n")
	except KeyboardInterrupt:
		print("\nKeybord Interrupt (<Ctrl>+C)\n")
		loop = False
