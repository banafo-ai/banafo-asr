#!/bin/bash

curr_dir="/usr/local/auto_uploader"

api_key=$curr_dir"/apiKey.txt"
app=$curr_dir"/python/auto_uploader.py"

#export apiKey=$api_key

if [ -f $api_key ]; then
	if [ -f $app ]; then
		$app '-u'
	else
		echo "ERROR!!!"
	fi
else
	echo
	echo "You don't have 'apiKey.txt' file with your API_KEY from 'https://banafo.ai'!!!"
	echo "First execute a file 'install.sh' in the current directory"
	echo
	exit 1
fi

