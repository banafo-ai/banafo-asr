#!/bin/bash

. ./config.sh

tests_dst1='/usr/local/auto_uploader/test1/'
tests_dst2='/usr/local/auto_uploader/test2/'
tests_dst3='/usr/local/auto_uploader/test3/'

localUri="ws://localhost:6006"
apiKey="apiKey from banafo.ai"
badApiKey=${apiKey::-4}

txt1="/usr/local/auto_uploader/txt1/"
txt2="/usr/local/auto_uploader/txt2/"
txt3="/usr/local/auto_uploader/txt3/"

if [ -d $python_dir ];then
	echo "* list"
	$cli -x list
else
	echo
	echo "You don't have auto_uploader installation!"
	echo "First you should start './install.sh'"
	echo
fi

mkdir -pv $tests_dst1
cp -v ../tests/audio_files/* $tests_dst1

TOOL=$python_dir"auto_uploader.py"

echo
echo "============================================================================================================================="
echo "Starting Tests..."
echo

TESTS=(
    "-x list"
    "-x insert --path=$tests_dst1 --uri=$localUri --txt=$txt1"
    "-x upload --log-type console --log-level debug"
    "-x pending"
    "-x success"
    "-x errors"
    "-x delete"
    "-x insert --path=$tests_dst1 --api=$apiKey --txt=$txt2"
    "-x upload --log-type console --log-level debug"
    "-x pending"
    "-x success"
    "-x errors"
    "-x delete"
    "-x insert --path=$tests_dst1 --api=$apiKey --txt=$txt3 --http 1"
    "-x upload --log-type console --log-level debug"
    "-x pending"
    "-x success"
    "-x errors"
    "-x delete"
    "-x insert --path=$tests_dst1 --api=$badApiKey --txt=$txt3 --http 1"
    "-x upload --log-type console --log-level debug"
    "-x pending"
    "-x success"
    "-x errors"
    "-x delete"
    "-x get --api=$apiKey --log-level debug --log-type console"
    "-x pending"
    "-x success"
    "-x errors"
    "-x remove --id=1"
    "-x delete"
)

for i in "${!TESTS[@]}"; do
    echo
    echo "============================================================================================================================="
    echo "Running test $((i+1)) with arguments: ${TESTS[$i]}"
    echo

    $TOOL ${TESTS[$i]}
    RESULT=$?

    if [ $RESULT -eq 0 ]; then
        echo -e "Test $((i+1)): ${GREEN}PASS${NC}"
    else
        echo -e "Test $((i+1)): ${RED}FAIL${NC}"
    fi
done

rm -fvR $tests_dst1 $txt1 $txt2 $txt3

echo "============================================================================================================================="
echo "Running 'auto_uploader_events' tests:"

sudo systemctl stop auto_uploader_events.service

mkdir -pv $tests_dst1
mkdir -pv $tests_dst2
mkdir -pv $tests_dst3

$TOOL -x insert --path=$tests_dst1 --uri=$localUri --txt=$txt1
$TOOL -x insert --path=$tests_dst2 --api=$apiKey --txt=$txt2
$TOOL -x insert --path=$tests_dst3 --api=$apiKey --txt=$txt3 --http 1 --res-interval 15 --res-attempts 4

sudo systemctl start auto_uploader_events.service
sudo systemctl status auto_uploader_events.service

sleep 5
cp -v ../tests/audio_files/* $tests_dst1
cp -v ../tests/audio_files/* $tests_dst2
cp -v ../tests/audio_files/* $tests_dst3

sleep 60
$TOOL -x success
$TOOL -x errors
$TOOL -x pending

sudo systemctl start auto_uploader_events.service
sudo rm -fvR $tests_dst1 $tests_dst2 $tests_dst3 $txt1 $txt2 $txt3
$TOOL -x delete
$TOOL -x remove --id 1
$TOOL -x remove --id 2
$TOOL -x remove --id 3
