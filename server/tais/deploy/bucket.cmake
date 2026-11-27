cmake_minimum_required(VERSION 3.21)
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/recording.cmake")
if(NOT STACK)
    message(FATAL_ERROR "Specify -DSTACK=<CloudFormation stack name>")
endif()
execute_process(COMMAND aws cloudformation deploy --stack-name "${STACK}"
    --template-file "${CMAKE_CURRENT_LIST_DIR}/bucket.yaml"
    --parameter-overrides "RetentionDays=${retention_days}"
    COMMAND_ERROR_IS_FATAL ANY)
