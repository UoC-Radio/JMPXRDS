#!/bin/bash
TOP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
DAEMON_LOGFILE=/tmp/jmpxrds-test-main-$$
COMMAND_LOGFILE=/tmp/jmpxrds-test-cmd-$$
DYNPS_PID=0
DYNRT_PID=0
TESTS_PASSED=0
TESTS_FAILED=0

# Console output

function pr_info {
	echo -e "\e[36m${@}\e[0m"
}

function pr_fail {
	echo -e "\e[31m${@}\e[0m"
}

function pr_pass {
	echo -e "\e[32m${@}\e[0m"
}


# Configuration tests

function run_config_test {
	local TEST_STATUS=0
	local JMPXRDS_STATUS=0

	${TOP_DIR}/$@ &> ${COMMAND_LOGFILE}
	if [[ $? == 0 ]]; then
		TEST_STATUS=1
	else
		TEST_STATUS=$?
	fi

	if [[ -r /run/user/$(id -u)/jmpxrds.sock ]]; then
		pidof jmpxrds &> /dev/null
		if [[ $? == 0 ]]; then
			JMPXRDS_STATUS=1;
		fi
	fi

	if [[ ${TEST_STATUS} == 1 && ${JMPXRDS_STATUS} == 1 ]]; then
		pr_pass "[PASSED] ${@}"
		TESTS_PASSED=$[${TESTS_PASSED} + 1]
	else
		pr_fail "[FAILED] ${@}"
		TESTS_FAILED=$[${TESTS_FAILED} + 1]
		cat ${DAEMON_LOGFILE}
		cat ${COMMAND_LOGFILE}

		# If JMPXRDS died the run is over
		if [[ ${JMPXRDS_STATUS} == 0 ]]; then
			return 1
		fi
	fi

	return 0
}

# FMMod tests
function test_fmmod {
	run_config_test fmmod_tool
	if [[ $? == 1 ]]; then
		return 1
	fi
	run_config_test fmmod_tool -g
	if [[ $? == 1 ]]; then
		return 1
	fi
	run_config_test fmmod_tool -s 1
	if [[ $? == 1 ]]; then
		return 1
	fi
	sleep 1
	run_config_test fmmod_tool -s 2
	if [[ $? == 1 ]]; then
		return 1
	fi
	sleep 1
	run_config_test fmmod_tool -s 3
	if [[ $? == 1 ]]; then
		return 1
	fi
	sleep 1
	run_config_test fmmod_tool -e 1
	if [[ $? == 1 ]]; then
		return 1
	fi
	sleep 1
	run_config_test fmmod_tool -f 0
	if [[ $? == 1 ]]; then
		return 1
	fi
	sleep 1
	run_config_test fmmod_tool -a 45 -m 80 -p 4 -r 3 -c 60 -s 0 -f 1 -e 0
	if [[ $? == 1 ]]; then
		return 1
	fi
	sleep 1
	run_config_test fmmod_tool -g
	if [[ $? == 1 ]]; then
		return 1
	fi
	return 0
}

# RDS tests
function test_rds {
	local TEST_PID=0

	run_config_test rds_tool
	if [[ $? == 1 ]]; then
		return 1
	fi
	run_config_test rds_tool -g
	if [[ $? == 1 ]]; then
		return 1
	fi
	run_config_test rds_tool -e -rt 123 -pi 0x1001 -pty 0 -ptyn test -ecc 0xe1 -lic 0x70 -tp 0 -ta 0 -ms 1 -di 0x9
	if [[ $? == 1 ]]; then
		return 1
	fi
	sleep 2
	run_config_test rds_tool -g
	if [[ $? == 1 ]]; then
		return 1
	fi
	run_config_test rds_tool -d
	if [[ $? == 1 ]]; then
		return 1
	fi
	run_config_test rds_tool -g
	if [[ $? == 1 ]]; then
		return 1
	fi
	run_config_test rds_tool -e -ps 123
	if [[ $? == 1 ]]; then
		return 1
	fi
	sleep 2

	touch /tmp/jmpxrds-test-dps-$$
	run_config_test rds_tool -dps /tmp/jmpxrds-test-dps-$$ -dt 15 &
	TEST_PID=$!
	sleep 1
	echo "  TEST123 abcdefg ABCDEFGHTEST " > /tmp/jmpxrds-test-dps-$$
	wait ${TEST_PID}
	if [[ $? == 1 ]]; then
		return 1
	fi
	rm /tmp/jmpxrds-test-dps-$$

	touch /tmp/jmpxrds-test-drt-$$
	run_config_test rds_tool -drt /tmp/jmpxrds-test-drt-$$ -dt 35 &
	TEST_PID=$!
	sleep 1
	echo " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz" > /tmp/jmpxrds-test-drt-$$
	echo "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqrstuvwxyz " >> /tmp/jmpxrds-test-drt-$$
	wait ${TEST_PID}
	if [[ $? == 1 ]]; then
		return 1
	fi
	rm /tmp/jmpxrds-test-drt-$$

	return 0
}

# RTP Tests
function test_rtp {
	run_config_test rtp_tool
	if [[ $? == 1 ]]; then
		return 1
	fi
	run_config_test rtp_tool -g
	if [[ $? == 1 ]]; then
		return 1
	fi
	run_config_test rtp_tool -a 127.0.0.1
	if [[ $? == 1 ]]; then
		return 1
	fi
	sleep 5
	run_config_test rtp_tool -g
	if [[ $? == 1 ]]; then
		return 1
	fi
	run_config_test rtp_tool -r 127.0.0.1
	if [[ $? == 1 ]]; then
		return 1
	fi
	run_config_test rtp_tool -g
	if [[ $? == 1 ]]; then
		return 1
	fi
}

# The main thing
# ARGS:
# 1-> Sampling rate for jack
# 2-> Number of samples per period
# 3-> Set to 1 to skip further tests
function test_run {
	local COUNT=5

	killall -9 jmpxrds &> /dev/null
	killall -9 jackd &> /dev/null

	rm /dev/shm/FMMOD_CTL_SHM &> /dev/null
	rm /dev/shm/RDS_ENC_SHM &> /dev/null
	rm /dev/shm/RTP_SRV_SHM &> /dev/null
	rm /run/user/$(id -u)/jmpxrds.sock &> /dev/null

	echo "" > ${DAEMON_LOGFILE}

	pr_info "Starting jack with rate ${1} and period size ${2}"

	(jackd --no-realtime -m -d dummy -r ${1} -p ${2} &> ${DAEMON_LOGFILE} &)

	jack_wait -w -t 5 &> /dev/null
	if [[ $? != 0 ]]; then
		pr_fail "Failed to start jackd"
		return 1
	fi

	pr_info "Starting jmpxrds"

	(${TOP_DIR}/jmpxrds &> ${DAEMON_LOGFILE} &)

	while [[ ${COUNT} -gt 0 ]]
	do
		if [[ -r /run/user/$(id -u)/jmpxrds.sock ]]; then
			if [[ ${3} == 0 ]]; then
				test_rds
				if [[ $? == 1 ]]; then
					break
				fi
				test_fmmod
				if [[ $? == 1 ]]; then
					break
				fi
				test_rtp
				if [[ $? == 1 ]]; then
					break
				fi
			fi
			killall jmpxrds &> /dev/null
			sleep 2
			killall jackd &> /dev/null
			sleep 2
			return 0
		else
			sleep 2
		fi
		COUNT=$[${COUNT} - 1]
	done

	if [[ ${COUNT} == 0 ]]; then
		pr_fail "Failed to start jmpxrds"
	fi
	cat ${DAEMON_LOGFILE}
	killall jackd &> /dev/null
	sleep 2
	return 2
}

function gather_coverage_results {
	mkdir ${TOP_DIR}/.gcov_reports &> /dev/null
	rm .gcov_reports/* &> /dev/null
	cd ${TOP_DIR}
	gcov * &> /dev/null
	cp ${TOP_DIR}/*.gcov ${TOP_DIR}/.gcov_reports
}

function print_summary {
	pr_info "Test results"
	pr_info "============"
	pr_info "PASSED: ${TESTS_PASSED}"
	pr_info "FAILED: ${TESTS_FAILED}"
}

if [[ ${1} == full ]]; then
	pr_info "Running a full test set"
	pr_info "======================="
	test_run 48000 512 0
	test_run 48000 1024 0
	test_run 48000 2048 0
	test_run 48000 4096 0
	test_run 96000 1024 0
	test_run 96000 2048 0
	test_run 96000 4096 0
	test_run 192000 1024 0
	test_run 192000 2048 0
	test_run 192000 4096 0
else
	pr_info "Running a quick test set for coverage analysis"
	pr_info "=============================================="
	test_run 48000 1024 0
	gather_coverage_results
fi

print_summary
