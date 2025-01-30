#!/bin/bash

bold=$(tput bold)
normal=$(tput sgr0)

# Define colors
RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'  # No Color (reset)

install_dir="/usr/local/auto_uploader"
python_dir=$install_dir"/python/"

curr_dir=$(pwd)
parent_dir=$(dirname "$curr_dir")

# events, timer(use service-timer of the systemd)
m1="events"
m2="timer"
mode=$m1

# banafo_mode=0
banafo_mode=2 # Banafo ASR Server !
#auto_detect_ans=''
auto_detect_ans='y' # Auto detect !

exec_sh=$curr_dir"/auto_uploader.sh"
exec_sh_2=$curr_dir"/auto_uploader_events.sh"
timer="auto_uploader.timer"
service="auto_uploader.service"
service_v2="auto_uploader_events.service"

# /usr/lib/systemd/system/ ??? intead /etc/systemd/system (have only links here ???)
# differently for different OS ???
etc_sys="/etc/systemd/system"
etc_sys_conf=$etc_sys"/"$service
etc_sys_conf_v2=$etc_sys"/"$service_v2
etc_sys_timer=$etc_sys"/"$timer

api_dir=$curr_dir"/apiKey.txt"
cli=$parent_dir"/python/auto_uploader.py"
ast_def_path=("/var/spool/asterisk/voicemail/" "/var/spool/asterisk/recording/" "/var/spool/asterisk/monitor/" )
fs_def_path=("/usr/local/freeswitch/recordings/" "/usr/local/freeswitch/storage/voicemail/" )
pip3_depends=("pydub" "requests" "websockets" "tabulate" "pyinotify" "argparse" "logging")
txt_dir="/usr/local/auto_uploader/txt/"
curr_db_path=$install_dir"/auto_uploader.db"
py_modules_list="requirements.txt"

usr=`stat -c '%U' .`
grp=`stat -c '%G' .`

checking_systemd() {
	res=`which systemctl`
	if [ -z "$res" ];then
		echo
		echo "The 'systemd' doesn't find here!"
		echo
		exit
	fi
}

checking_db_file() {
	if [ -f $curr_db_path ];then
		echo
		echo "...${1} is detected!"
		echo
	else
		echo
		echo "...The ${1} is not detected!"
		echo
	fi
}

checking_pip3_depends() {
#pip3 install pydub requests websockets tabulate pyinotify
#pip3 install pysqlite3 ???
	pip3 install --upgrade pip #???? first ???
	for el in ${pip3_depends[@]}; do
		if ! pip3 show $el >/dev/null; then
			pip3 install $el
		fi
	done
}

create_api_dir() {
	echo
	echo "${bold}Enter your API_KEY from the 'https://banafo.ai' here:${normal}"
	read api_key

	# Create a file with the input string
	echo "$api_key" > $api_dir

	echo
	echo "The file 'apiKey.txt' was created with your API_KEY: '$api_key'"
	echo
}

installation_dialog_1() {
	echo
	echo "What do you what to use ?"
	echo " [1] ${bold}Banafo API${normal} to the Banafo site (https://banafo.ai) "
	echo " [2] ${bold}Banafo ASR server${normal} - local or remote "
	echo
	echo "   Your choise [1 or 2]:"
	echo
	read banafo_mode
}

installation_dialog_2() {
	echo
	echo "Do you want to do ${bold}'auto detect'${normal} for some PBXes - Asterisk,FreeSWITCH,... ?"
	echo
	echo " Your choise [Y or N]:"
	echo
	read auto_detect_ans
}

installation_prepare() {
	if [ -d $install_dir ];then
		echo
		echo "You already installed the Banafo 'auto_uploader' !"
		echo
		echo "if you want to re-install you must execute './uninstall.sh' first !"
		echo
		exit
	else
		echo
		echo "Start installation..."
		echo
		sudo mkdir -pv $install_dir
		sudo mkdir -pv $python_dir
		echo
		echo
	fi
}
