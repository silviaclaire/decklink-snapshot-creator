#include "include/spdlog/spdlog.h"
#include "include/spdlog/sinks/daily_file_sink.h"
#include "include/spdlog/sinks/rotating_file_sink.h"


int main(int argc, char* argv[])
{
	std::string	 logDirectory = argv[1];

	if (argc == 2)
	{
		// ファイル上限サイズを1KBに指定して、ファイル2個までローテーション
		auto max_size = 1024;
		auto max_files = 2;
		auto logger = spdlog::rotating_logger_mt("SnapShotCreator", logDirectory+"\\"+"SnapShotCreator.log", max_size, max_files);
		spdlog::set_default_logger(logger);
	}
	else
	{
		// ローテーション時刻は次の1分に指定
		int hour = atoi(argv[2]);
		int min = atoi(argv[3]);
		// 日ごとにローテーション（ファイル2個まで）
		auto logger = spdlog::daily_logger_mt("SnapShotCreator", logDirectory+"\\"+"SnapShotCreator.log", hour, min, false, 2);
		spdlog::set_default_logger(logger);
	}

	// Get command line options
	for (int i = 1; i < 100; i++)
	{
		std::this_thread::sleep_for(std::chrono::seconds(1));
		spdlog::info("Test log rotation");
	}

	return 0;
}