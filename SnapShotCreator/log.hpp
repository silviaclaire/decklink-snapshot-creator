#ifndef LOG_HPP
#define LOG_HPP

#include <iostream>
#include <sstream>
#include <memory>
#include <mutex>

#include "utils.h"

template <typename ostreamT = std::ostream>
class basic_logger {
public:
	enum log_level {
		DEBUG,
		INFO,
		WARNING,
		ERR
	};

private:
	log_level _minLevel;
	ostreamT& _os;

	template<class T>
	void _log(const T& x) {
		_os << x;
	}

	template <class T,class... Ts>
	void _log(const T& x,Ts... args) {
		_log(x);
		_log(args...);
	}

public:
	basic_logger(log_level minLevel = INFO, ostreamT& os = std::cout) :
	  _minLevel(minLevel),_os(os) {}

	log_level minLevel() const {
		return _minLevel;
	}

	template <class... Ts>
	basic_logger& operator()(log_level level,Ts... args) {
		if(level >= (int)_minLevel) {
			std::string prefix =
				level == DEBUG   ?   " [DEBUG  ] " :
				level == INFO    ?   " [INFO   ] "   :
				level == WARNING ?   " [WARNING] " :
				/*level == ERR  */   " [ERROR  ] ";
			_log(CurrentDateTime("%Y-%m-%d %H:%M:%S"),prefix,args...,"\n");
			_os.flush();
		}
		return *this;
	}
	template <class... Ts>
	basic_logger& d(Ts... args) {
		return operator()(DEBUG,args...);
	}
	template <class... Ts>
	basic_logger& i(Ts... args) {
		return operator()(INFO,args...);
	}
	template <class... Ts>
	basic_logger& w(Ts... args) {
		return operator()(WARNING,args...);
	}
	template <class... Ts>
	basic_logger& e(Ts... args) {
		return operator()(ERR,args...);
	}
};

using Logger = basic_logger<>;

// TODO(high): write log to file
static Logger	logger{Logger::DEBUG, std::cout};

#endif
