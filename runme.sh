#!/bin/bash

gcc myinit.c -o myinit;

touch /tmp/in
touch /tmp/out
touch /tmp/myconf

echo /bin/sleep 5 /tmp/in /tmp/out > /tmp/myconf
echo /bin/sleep 10 /tmp/in /tmp/out >> /tmp/myconf
echo /bin/sleep 15 /tmp/in /tmp/out >> /tmp/myconf

./myinit /tmp/myconf

sleep 1

echo -e "---RUNME started... checking 3 processes---" > result.txt
ps -ef | grep "/bin/sleep" | grep -v grep >> result.txt

pkill -f "/bin/sleep 15"

echo -e "\n---One process has been killed... checking restart---" >> result.txt
ps -ef | grep "/bin/sleep" | grep -v grep >> result.txt

sleep 15

echo /bin/sleep 7 /tmp/in /tmp/out > /tmp/myconf

killall -HUP myinit

echo -e "\n---SIGHUP has been sent... checking 1 process---" >> result.txt

ps -ef | grep "/bin/sleep" | grep -v grep >> result.txt

echo -e "\n---LOGS---" >> result.txt

cat /tmp/myinit.log >> result.txt
killall myinit
