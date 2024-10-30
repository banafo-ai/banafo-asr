#!/bin/bash

. ./config.sh

sudo rm -fvR $install_dir
sudo rm -fv $curr_db_path
sudo rm -fv $api_dir

checking_systemd

# disable and stop in systemd
if [ "$mode" == "$m2" ];then
	sudo systemctl stop $service
	sudo systemctl disable $service

	sudo systemctl stop $timer
	sudo systemctl disable $timer

	sudo systemctl daemon-reload

	sudo rm -vf $etc_sys_conf
	sudo rm -vf $etc_sys_timer
elif [ "$mode" == "$m1" ];then
	sudo systemctl stop $service_v2
	sudo systemctl disable $service_v2

	sudo systemctl daemon-reload

	sudo rm -vf $etc_sys_conf_v2
else
	echo
	echo "Invalid uninstall mode!"
	echo
fi
