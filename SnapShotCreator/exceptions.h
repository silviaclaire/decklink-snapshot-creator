#pragma once

#include <stdexcept>

class InitializationError : public std::runtime_error
{
public:
    InitializationError(const std::string& message) throw();
    InitializationError(const char *message) throw();

};
InitializationError::InitializationError(const std::string& message) throw(): std::runtime_error(message) {};
InitializationError::InitializationError(const char *message) throw(): std::runtime_error(message) {};


class InvalidRequest : public std::runtime_error
{
public:
    InvalidRequest(const std::string& message) throw();
    InvalidRequest(const char *message) throw();

};
InvalidRequest::InvalidRequest(const std::string& message) throw(): std::runtime_error(message) {};
InvalidRequest::InvalidRequest(const char *message) throw(): std::runtime_error(message) {};


class InvalidParams : public std::runtime_error
{
public:
    InvalidParams(const std::string& message) throw();
    InvalidParams(const char *message) throw();

};
InvalidParams::InvalidParams(const std::string& message) throw(): std::runtime_error(message) {};
InvalidParams::InvalidParams(const char *message) throw(): std::runtime_error(message) {};


class CaptureError : public std::runtime_error
{
public:
    CaptureError(const std::string& message) throw();
    CaptureError(const char *message) throw();

};
CaptureError::CaptureError(const std::string& message) throw(): std::runtime_error(message) {};
CaptureError::CaptureError(const char *message) throw(): std::runtime_error(message) {};
