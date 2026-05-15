#!/bin/bash
uriscv config_machine.json &
sleep 1
kill -9 %1
