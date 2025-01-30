#!/bin/bash

. ./config.sh
. ./auto_detect.sh

detect_os
echo
echo
echo "OS: "$os_name
echo "Application mode: "$mode
echo "User:"$usr",Group:"$grp
echo

if [ $os_name == "debian" ] || [ $os_name == "ubuntu" ];then
	# Debian/Ubuntu packets
	sudo apt-get install -y git python3 python3-pip python3-pydub python3-tabulate python3-websockets python3-pyinotify \
	python3-requests python3-numpy python3-logging-tree libsqlite3-mod-spatialite ffmpeg
elif [ $os_name == "fedora" ];then
	# Fedora (Red Hat) packets
	sudo yum -y install git python3 python3-pip ffmpeg-free
	#ffmpeg  < f40 ???
	printf "\nlogging\n" >> $py_modules_list
fi

# check python3 version
detect_python3_version
if [ $p3_major -eq 3 ];then
	if [ $p3_minor -ge 12 ]; then
		# install 'pyasyncore'
		git clone https://github.com/simonrob/pyasyncore.git
		python3 -m pip install pyasyncore
	fi
fi

# Need Python3 modules/packets:
#checking_pip3_depends
sudo pip3 install --upgrade pip
sudo pip3 install --upgrade -r $py_modules_list

#installation_dialog_1

if [ -z $banafo_mode ];then
	echo "Error!Exit!"
	exit
fi

if [ $banafo_mode -eq 1 ];then
	# insert apiKey in the file './apiKey.txt'
	create_api_dir
	uri="None"
elif [ $banafo_mode -eq 2 ];then
	api_key="None"
	# when you want to use other uri by default, put arg1 in the './install.sh'
	if [ -z $1 ];then
		uri="ws://127.0.0.1:6006"
	else
		uri=$1
	fi
else
	echo "Error!Exit!"
	exit
fi

#installation_dialog_2

# create a '/usr/local/auto_uploader' and '../python'
installation_prepare

# init DB
sudo $cli -x list

# when you don't want to use 'auto_detect', put arg2 in the './install.sh'
if [ -z $2 ];then
	if [ -z $auto_detect_ans ];then
		auto_detect_ans='N' 
	fi

	if [ $auto_detect_ans == 'Y' ] | [ $auto_detect_ans == 'y' ];then
		# detect PBX - asterisk,freeswitch,...
		echo
		echo "Auto detect (asterisk,freeswitch,...) :"
		echo

		detect_asterisk $api_key $uri
		detect_freeswitch $api_key $uri
	fi
fi

sudo cp -v $exec_sh $install_dir

if [ -f $api_dir ];then
	sudo cp -v $api_dir $install_dir
fi

sudo cp -v $parent_dir"/python/auto_uploader.py" $python_dir
sudo cp -v $parent_dir"/python/auto_uploader_db.py" $python_dir
sudo cp -v $parent_dir"/python/banafo_client.py" $python_dir
sudo cp -v $parent_dir"/python/auto_uploader_events.py" $python_dir

sudo sed -i "s|^TXT_DIR = \"./txt/\"|TXT_DIR = \"$install_dir/txt/\"|" $python_dir"auto_uploader.py"

# change owner - user:group
if [ $usr != "root" ];then
    sudo chown -vR $usr:$grp $install_dir
fi

if [ $# -ge 2  ];then
	$cli -x insert --path=$2 --uri=$1 --txt=$txt_dir
fi

checking_systemd

if [ "$mode" == "$m2" ];then
	# config,install and start of Banafo auto_uploader as service&timer in systemd
	sudo cp -v $curr_dir"/"$service $etc_sys_conf
	sudo cp -v $curr_dir"/"$timer $etc_sys_timer

	# enable and start in systemd
	sudo systemctl enable $service
	sudo systemctl enable $timer

	sudo systemctl daemon-reload

	sudo systemctl start $service
	sudo systemctl start $timer
elif [ "$mode" == "$m1" ];then
	# config,install and start of Banafo auto_uploader_events as service in systemd
	sudo cp -v $curr_dir"/"$service_v2 $etc_sys_conf_v2

	# enable and start in systemd
	sudo systemctl enable $service_v2
	sudo systemctl daemon-reload
	sudo systemctl start $service_v2
else
	echo 
	echo "Invalid install mode!"
	echo
	exit
fi
