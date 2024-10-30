#!/bin/bash

. ./config.sh

os_name=''

detect_os() {
	os_name=`cat /etc/os-release | sort |grep -m 1 "ID=" | awk -F"ID=" '{print $2}'`
}

python3_version=''
p3_major=0
p3_minor=0

detect_python3_version() {
	python3_version=`python3 --version | awk -F" " '{print $2}'`
	p3_major=`echo $python3_version | awk -F"." '{print $1}'`
	p3_minor=`echo $python3_version | awk -F"." '{print $2}'`
}

_detect_() {
	local func_name=$1
	local api_key=$2
	local uri=$3
	shift 3
	local my_arr=("$@")

	echo
	echo "Auto detect - '${func_name}' ... :"

	for el in "${my_arr[@]}"; do
		if [ -d  $el ]; then
			echo
			echo "...The ${func_name} directory '${el}' was found!"
			echo
			#echo "${bold}Enter language here (en-US,...):${normal}"
			#read lang
			#if [ -z "$lang" ];then
				lang="None"
			#fi
			$cli -x insert --path=$el --lang=$lang --api=$api_key --uri=$uri --txt $txt_dir
		fi
	done
}

detect_asterisk() {
	ast_name="Asterisk"

	_detect_ "${ast_name}" "${1}" "${2}" "${ast_def_path[@]}"

	#checking_db_file $ast_name
}

detect_freeswitch() {
	fs_name="FreeSWITCH"

	_detect_ "${fs_name}" "${1}" "${2}" "${fs_def_path[@]}"

	#checking_db_file $fs_name
}
