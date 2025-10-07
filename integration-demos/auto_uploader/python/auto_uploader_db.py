import sqlite3
import time

import auto_uploader as auto_upl

UPLOADER_TABLE_NAME = 'auto_uploader_files'
UPLOADER_TABLE_PATHS = 'auto_uploader_paths'
UPLOADER_DB_FILE = "auto_uploader.db"
UPLOADER_DB_DIR = "/usr/local/auto_uploader/"
UPLOADER_DB = UPLOADER_DB_DIR + UPLOADER_DB_FILE

errors = ['wait for uploading','uploaded to Banafo','file size > 500MB','duration < 10sec',
		  'got transcriptions & finished','put result in TXT & finished',
		  'unknown error','','None result','no connection to server','queue is FULL']

def sqlite3_conn(db_name):
	conn = sqlite3.connect(db_name)
	cursor = conn.cursor()
	sqlite3_uploader_init(cursor)
	return conn,cursor

def sqlite3_close(conn,cursor):
	cursor.close()
	conn.close()

def sqlite3_exec(conn,cursor,query,record=None):
	try:
		if record is None:
			cursor.execute(query)
		else:
			cursor.execute(query, record)
		conn.commit()
	except sqlite3.Error as e:
		conn.rollback()
		auto_upl.logger.error(f"\nSQLite3 error: {e}")

def sqlite3_uploader_get_path_data(conn, cursor, _name):
	query = f"""SELECT * FROM {UPLOADER_TABLE_PATHS} WHERE path = ?"""

	sqlite3_exec(conn, cursor, query, (_name,))
	return [list(row) for row in cursor.fetchall()]

def sqlite3_uploader_get_path_data_from_id(conn, cursor, id):
	query = f"""SELECT * FROM {UPLOADER_TABLE_PATHS} WHERE id = ?"""

	sqlite3_exec(conn, cursor, query, (id,))
	return [list(row) for row in cursor.fetchall()]

def sqlite3_uploader_get_path_from_api(conn, cursor, api_key):
	query = f"""SELECT * FROM {UPLOADER_TABLE_PATHS} WHERE api_key = ?"""

	sqlite3_exec(conn, cursor, query, (api_key,))
	return [list(row) for row in cursor.fetchall()]

def sqlite3_uploader_get_paths(conn, cursor):
	query = f"""SELECT id, path, asr_lang, api_key, uri, get_result_interval, get_result_attempts, res_path, http_flag, streaming_flag 
				FROM {UPLOADER_TABLE_PATHS}"""

	sqlite3_exec(conn, cursor, query)
	return [list(row) for row in cursor.fetchall()]

def sqlite3_uploader_upload_files(conn, cursor, flag, op='='):
	query = f"""
	SELECT id, file_path, asr_lang, 
		strftime('%Y-%m-%d %H:%M:%S', created_ts, 'unixepoch', 'localtime') AS created_date, 
		strftime('%Y-%m-%d %H:%M:%S', uploaded_ts, 'unixepoch', 'localtime') AS uploaded_date, 
		flag, 
		strftime('%Y-%m-%d %H:%M:%S', finished_ts, 'unixepoch', 'localtime') AS finished_date 
	FROM {UPLOADER_TABLE_NAME} 
	WHERE flag {op} ?
	"""

	sqlite3_exec(conn, cursor, query, (flag,))

	rows = []
	for row in cursor.fetchall():
		_id, _wf_name, _asr_lang, _date_1, _date_2, _flag, _date_3 = row

		_abs_flag = abs(_flag)
		_flag_msg = errors[_abs_flag]

		rows.append([_id, _wf_name, _asr_lang, _date_1, _date_2, _date_3, _flag, _flag_msg])

	return rows

def sqlite3_uploader_get_file_status_eq(conn, cursor, flag, op='='):
	query = f"SELECT id, file_path, asr_lang, created_ts, uploaded_ts, flag, file_id \
				FROM {UPLOADER_TABLE_NAME} WHERE flag {op} ?"

	sqlite3_exec(conn, cursor, query, (flag,))
	return [list(row) for row in cursor.fetchall()]

def sqlite3_uploader_get_file_status_path_id(conn, cursor, path_id, flag, op='='):
	query = f"""
	SELECT id, file_path, asr_lang, created_ts, uploaded_ts, flag, file_id, res_attempts
	FROM {UPLOADER_TABLE_NAME} 
	WHERE path_id = ? AND flag {op} ?
	"""

	sqlite3_exec(conn, cursor, query, (path_id, flag))
	return [list(row) for row in cursor.fetchall()]

def sqlite3_uploader_get_file_status_or(conn, cursor, flag1, flag2):
	query = f"""
	SELECT id, file_path, asr_lang, created_ts, uploaded_ts, flag, file_id 
	FROM {UPLOADER_TABLE_NAME} 
	WHERE flag = ? OR flag = ?
	"""

	sqlite3_exec(conn, cursor, query, (flag1, flag2))
	return [list(row) for row in cursor.fetchall()]

def sqlite3_uploader_check_file(conn, cursor, fn):
	query = f"SELECT id FROM {UPLOADER_TABLE_NAME} WHERE file_path = ?"

	sqlite3_exec(conn, cursor, query, (fn,))
	row = cursor.fetchone()

	return row[0] if row else 0

def sqlite3_uploader_check_path(conn, cursor, fn):
	query = f"SELECT id FROM {UPLOADER_TABLE_PATHS} WHERE path = ?"

	sqlite3_exec(conn, cursor, query, (fn,))
	row = cursor.fetchone()

	return row[0] if row else 0

def sqlite3_uploader_delete_path(conn,cursor,_id):
	query = f"""DELETE FROM {UPLOADER_TABLE_PATHS} where id = ?"""
	sqlite3_exec(conn, cursor, query, (str(_id)))

def sqlite3_uploader_delete_files(conn,cursor):
	query = f"""DELETE FROM {UPLOADER_TABLE_NAME}"""
	sqlite3_exec(conn, cursor, query)

def sqlite3_uploader_set_flag(conn, cursor, _id, _file_id, flag=1):
	if _id == 0:
		return

	query = f"""UPDATE {UPLOADER_TABLE_NAME} SET flag = ?, file_id = ?, uploaded_ts = ?"""

	if flag in {4, 5}:
		query += ", finished_ts = ?"

	query += " WHERE id = ?"

	params = [flag, _file_id, int(time.time())]
	if flag in {4, 5}:
		params.append(int(time.time()))
	params.append(_id)

	sqlite3_exec(conn, cursor, query, params)

def sqlite3_uploader_set_res_attempts(conn, cursor, _id, _res_attempts):
	if _id == 0:
		return

	query = f"""
	UPDATE {UPLOADER_TABLE_NAME}
	SET res_attempts = ?, uploaded_ts = ?
	WHERE id = ?"""

	sqlite3_exec(conn, cursor, query, (_res_attempts, int(time.time()), _id))

def sqlite3_uploader_init(cursor):
	cursor.execute(f'''
    CREATE TABLE IF NOT EXISTS {UPLOADER_TABLE_PATHS} (
        id integer PRIMARY KEY,
        path varchar(255),
        asr_lang varchar(32),
        api_key varchar(255),
        uri varchar(255),
        get_result_interval integer,
        get_result_attempts integer,
        res_path varchar(255),
        http_flag integer default 0,
        streaming_flag integer default 1,
        created_ts integer
    )
	''')

	cursor.execute(f'''
    CREATE TABLE IF NOT EXISTS {UPLOADER_TABLE_NAME} (
        id integer PRIMARY KEY,
        file_path varchar(255),
        asr_lang varchar(32),
        file_id varchar(255),
        created_ts integer,
        uploaded_ts integer,
        finished_ts integer,
        flag integer,
        path_id integer,
        res_attempts integer
    )
	''')

def sqlite3_uploader_insert_path(conn, cursor, dir_name, asr_lang, api_key, uri, res_interval=0, res_attempts=0, res_path=None, http_flag=0, streaming_flag=1):
	_id = sqlite3_uploader_check_path(conn, cursor, dir_name)

	if _id == 0:
		query = f'''
			INSERT INTO {UPLOADER_TABLE_PATHS} (path, asr_lang, api_key, uri, get_result_interval, 
												get_result_attempts, res_path, created_ts, http_flag, streaming_flag)
			VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
		'''

		record = (
			dir_name, asr_lang or '', api_key or '', uri or '',
			res_interval, res_attempts, res_path, int(time.time()), int(http_flag), streaming_flag
		)
	else:
		query = f'''
			UPDATE {UPLOADER_TABLE_PATHS}
				SET asr_lang = ?, api_key = ?, uri = ?, get_result_interval = ?, 
				get_result_attempts = ?, res_path = ?, http_flag = ?, streaming_flag = ?
			WHERE id = ?'''

		record = (
			asr_lang or '', api_key or '', uri or '', 
			res_interval, res_attempts, res_path, int(http_flag), streaming_flag,_id
		)

	sqlite3_exec(conn, cursor, query, record)

def sqlite3_uploader_db(conn,cursor,file_id,wf_name,asr_lang,path_id,flag=0,res_attempts=0):
	record = (file_id, wf_name,asr_lang,int(time.time()),flag,path_id,res_attempts)
	if sqlite3_uploader_check_file(conn, cursor,wf_name) == 0:
		query = f'''
			INSERT INTO {UPLOADER_TABLE_NAME} (file_id, file_path,asr_lang,created_ts,flag,path_id,res_attempts)
			VALUES (?, ? , ? , ? , ? , ? , ?)'''

		sqlite3_exec(conn, cursor, query, record)

	return sqlite3_uploader_check_file(conn, cursor,wf_name)
