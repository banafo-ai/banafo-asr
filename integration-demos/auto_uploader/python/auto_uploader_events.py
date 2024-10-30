#!/usr/bin/env python3

import os
import sys
import time
import signal
import asyncio
import argparse
import pyinotify
#import threading
#from contextlib import closing

import auto_uploader_db as db
import auto_uploader as auto_upl

CHECKER_TIME_INTERVAL = 300

class asyncio_data:
    def __init__(self , path_id, path, res_path, interval):
        self.path_id = path_id
        self.path = path
        self.res_path = res_path
        self.checker_time_interval = interval

    def put_queue(self, q):
        self.queue = q

    def put_dbdata(self, conn, cursor):
        self.conn = conn
        self.cursor = cursor

async def auto_upl_new_file_process_async(directory, file_name, dt):
    start_time = time.time()

    if not os.path.isfile(file_name):
        auto_upl.logger.error(f"File '{file_name}' does not exist in directory '{directory}'")
        return

    conn, cursor = dt.conn, dt.cursor

    check_file_id = db.sqlite3_uploader_check_file(conn, cursor, file_name)
    if check_file_id > 0:
        return

    auto_upl.logger.info(f"New file detected in {directory}: {file_name}")

    path_data = db.sqlite3_uploader_get_path_data(conn, cursor, directory)
    if not path_data:
        auto_upl.logger.warning(f"No data found for directory '{directory}' in the database")
        return

    path_id, asr_lang, api_key, uri, res_path, http_flag = (path_data[0][0],
                                                            path_data[0][2], 
                                                            path_data[0][3] or None, 
                                                            path_data[0][4] or None, 
                                                            path_data[0][7] or auto_upl.TXT_DIR, 
                                                            path_data[0][8])

    validated_file = auto_upl.auto_upl_file_validate(file_name)
    if validated_file is None:
        auto_upl.logger.error(f"File '{file_name}' is invalid or couldn't be processed")
        db.sqlite3_uploader_db(conn, cursor, "", file_name, asr_lang, path_id, -6)
        return

    insert_flag = auto_upl.auto_upl_insert_file(conn, cursor, validated_file, asr_lang, path_id)
    if insert_flag != 0 :
        auto_upl.logger.warning(f"Failed to insert file '{validated_file}' into the database, insert_flag: {insert_flag}")
        return

    file_id = db.sqlite3_uploader_check_file(conn, cursor, validated_file)

    if uri:
        res_producer = await auto_upl.auto_upl_queue_async_producer(dt.queue, validated_file, file_id, res_path)
        if res_producer is True:
            auto_upl.logger.info(f"The file '{validated_file}' was queued for upload to the ASR server [{file_id}]")
        else:
            db.sqlite3_uploader_set_flag(conn,cursor,file_id,"",-10)
    elif api_key:
        res_producer = await auto_upl.auto_upl_queue_async_producer(dt.queue, validated_file, file_id, res_path, api_key, asr_lang, http_flag)
        if res_producer is True:
            auto_upl.logger.info(f"The file '{validated_file}' was queued for upload to the Banafo [{file_id}]")
        else:
            db.sqlite3_uploader_set_flag(conn,cursor,file_id,"",-10)
    else:
        auto_upl.logger.error(f"The file '{validated_file}' wasn't uploaded! Missing API key or URI for file [{file_id}]")

    end_time = time.time()
    auto_upl.logger.info(f"Processing time for '{file_name}': {end_time - start_time:.2f} seconds")

class NewFileEventHandlerAsync(pyinotify.ProcessEvent):
    def __init__(self, data):
        super().__init__()
        self.data = data
        self.directory = data.path
        self.queue = data.queue

    def process_default(self, event):
        print ("==> ", event.maskname, ": ", event.pathname)

    def process_IN_CREATE(self, event):
        auto_upl.logger.info(f"event: IN_CREATE, {event.pathname}")

        # Delay to avoid processing a file before it's fully written (may help avoid zero file size)
        time.sleep(0.05)
        #asyncio.sleep(0.01)

        attr = auto_upl.auto_upl_get_file_attr(event.pathname)
        if attr is not None:
            file_size = attr.get('size', 0)
            auto_upl.logger.debug(f"File size detected: {file_size} bytes for {event.pathname}")

            # 44 bytes is typically the size of a WAV header, adjust for other formats if needed
            if file_size > 44:
                auto_upl.logger.info(f"Processing file: {event.pathname}")
                asyncio.create_task(auto_upl_new_file_process_async(self.directory, event.pathname, self.data))
            else:
                auto_upl.logger.warning(f"Empty or incomplete file detected: {event.pathname}")

    def process_IN_CLOSE_NOWRITE(self, event):
        auto_upl.logger.info(f"event: IN_CLOSE_NOWRITE, {event.pathname}")
        if os.path.isfile(event.pathname):
            auto_upl.logger.info(f"Processing closed file: {event.pathname}")
            asyncio.create_task(auto_upl_new_file_process_async(self.directory, event.pathname, self.data))

    def process_IN_MOVED_TO(self, event):
        auto_upl.logger.info(f"event: IN_MOVED_TO, {event.pathname}")
        asyncio.create_task(auto_upl_new_file_process_async(self.directory, event.pathname, self.data))

async def auto_upl_ev_checker_async(data):
    conn, cursor = data.conn , data.cursor
    while True:
        try:
            files = db.sqlite3_uploader_get_file_status_path_id(conn, cursor, data.path_id, -9, "<=")
            for row in files:
                file_id, file_path = row[0], row[1]
                auto_upl.logger.debug(f"Adding file to queue (requeuing) for processing (async): ID={file_id}, Path={file_path}")
                path_data = db.sqlite3_uploader_get_path_data_from_id(conn, cursor, data.path_id)
                asr_lang, api_key, http_flag = (path_data[0][2] or None, 
                                                path_data[0][3] or None, 
                                                path_data[0][8] or 0)
                
                res_producer = await auto_upl.auto_upl_queue_async_producer(data.queue, file_path, file_id, data.res_path, api_key, asr_lang, http_flag)
                if res_producer is True:
                    db.sqlite3_uploader_set_flag(conn,cursor,file_id,"",0)
                else:
                    db.sqlite3_uploader_set_flag(conn,cursor,file_id,"",-10)

            files_num = len(files)
            if files_num > 0 :
                auto_upl.logger.info(f"Checked and queued {len(files)} files for processing.")

        except Exception as e:
            auto_upl.logger.error(f"Error: {str(e)}", exc_info=True)

        await asyncio.sleep(data.checker_time_interval)

async def auto_upl_monitor_directory_async(data):
    auto_upl.logger.info(f"Starting to monitor directory (async): {data.path}")

    event_handler = NewFileEventHandlerAsync(data)
    wm = pyinotify.WatchManager()

    wm.add_watch(data.path, pyinotify.IN_CLOSE_NOWRITE|pyinotify.IN_MOVED_TO|pyinotify.IN_CREATE, rec=True, auto_add=True)

    notifier = pyinotify.AsyncioNotifier(wm, asyncio.get_event_loop(), default_proc_fun=event_handler)

    auto_upl.logger.info(f"Monitoring directory (async): {data.path}")

    try:
        checker_task = asyncio.create_task(auto_upl_ev_checker_async(data))
        while True:
            await asyncio.sleep(1)
    except asyncio.CancelledError:
        auto_upl.logger.info("Monitoring cancelled (async)")
    finally:
        notifier.stop()
        checker_task.cancel()
        await checker_task
        auto_upl.logger.info("Stopped monitoring (async)")

async def auto_upl_get_result_async(conn, cursor, data):
    path_id, _, _, api_key, _, res_interval, res_attempts, res_path = data[:8]

    auto_upl.logger.info(f"Starting result retrieval task for path_id: {path_id} with interval: {res_interval}s")

    while True:
        await asyncio.sleep(res_interval)
        auto_upl.logger.debug(f"Fetching result files for path_id: {path_id}")
        auto_upl.auto_upl_get_result_files_v2(conn, cursor, path_id, api_key, res_attempts, res_path)

def get_args():
    parser = argparse.ArgumentParser(
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
        add_help=True,
#        description="'auto_uploader_events.py' starts ",
        usage="./auto_uploader_events.py --thread-nums <NUM> --max-queue-size <SIZE>"
    )

    parser.add_argument(
        "-n",
        "--task-nums",
        type=int,
        default=auto_upl.MAX_TASKS,
        help="Tasks number in the Banafo ASR server mode",
    )

    parser.add_argument(
        "-m",
        "--max-queue-size",
        type=int,
        default=auto_upl.MAX_QUEUE_SIZE,
        help="MAX Queue size (max waiting audio files to uploading) in the Banafo ASR server mode",
    )

    parser.add_argument(
        "-t",
        "--checker-time-interval",
        type=int,
        default=CHECKER_TIME_INTERVAL,
        help="This parameter set time interval in the checker thread - how many time does it have between checks",
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
        default=auto_upl.LOG_FILE_MSIZE,
        help="log file max size: in MB",
    )

    parser.add_argument(
        "--log-file-counter",
        type=int,
        default=auto_upl.LOG_FILE_COUNTER,
        help="log file counter",
    )

    return parser.parse_args()

def auto_upl_signal_handler(sig, frame):
    print('Ctrl+C pressed, exiting gracefully **** ')
    sys.exit(0)

async def auto_upl_run_asyncio(paths, _args):
    for el in paths:
        id, path, _, api_key, uri, res_interval, _, res_path, http_flag = el
        asyncio_data_el = asyncio_data(id, path, res_path, _args.checker_time_interval)

        os.makedirs(res_path, exist_ok=True)

        queue = asyncio.Queue(maxsize=_args.max_queue_size)
        asyncio_data_el.put_queue(queue)

        conn, cursor = db.sqlite3_conn(db.UPLOADER_DB)
        asyncio_data_el.put_dbdata(conn, cursor)

        workers = asyncio.create_task(auto_upl_monitor_directory_async(asyncio_data_el))

        if api_key:
            if http_flag == 1:
                workers = [asyncio.create_task(auto_upl.auto_upl_http_worker(conn, cursor, queue, res_path)) for _ in range(_args.task_nums)]
                if (res_interval > 0):
                    workers = asyncio.create_task(auto_upl_get_result_async(conn, cursor, el))
            else:
                workers = [asyncio.create_task(auto_upl.auto_upl_wss_worker(conn, cursor, queue, res_path)) for _ in range(_args.task_nums)]
        elif uri:
            workers = [asyncio.create_task(auto_upl.auto_upl_asr_server_worker(conn, cursor, queue, uri, res_path)) for _ in range(_args.task_nums)]

    try:
        while True:
            await asyncio.sleep(1)
    finally:
        db.sqlite3_close(conn, cursor)

        await queue.join()
        await asyncio.gather(*workers)

def main():
    # Parse arguments
    _args = get_args()

    auto_upl.auto_upl_logging_setup(auto_upl.LogHandlerType(_args.log_type), _args.log, _args.log_level, _args.log_file_msize, _args.log_file_counter)
    auto_upl.logger.debug(f"Parsed arguments: {_args}")

    # Handle graceful shutdown signals
    signal.signal(signal.SIGINT, auto_upl_signal_handler)
    signal.signal(signal.SIGTERM, auto_upl_signal_handler)

    # Check if DB exists
    if not os.path.exists(db.UPLOADER_DB_DIR):
        auto_upl.logger.error("Database file is missing! Please run './install.sh' first.")
        sys.exit(0)

    # Connect to the database
    conn, cursor = db.sqlite3_conn(db.UPLOADER_DB)

    try:
        # Fetch paths from the database
        paths = db.sqlite3_uploader_get_paths(conn, cursor)

        # Initial scan and upload
        if not auto_upl.auto_upl_scan_and_upload_async(conn, cursor, paths, _args.task_nums, _args.max_queue_size):
            auto_upl.logger.error("Initial scan and upload failed, exiting.")
            sys.exit(1)

    finally:
        db.sqlite3_close(conn, cursor)

    asyncio.run(auto_upl_run_asyncio(paths, _args))

if __name__ == "__main__":
    main()