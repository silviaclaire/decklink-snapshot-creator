#include <httplib.h>
#include <Windows.h>
#include <stdio.h>
#include <json/json.h>

#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "include/spdlog/spdlog.h"
#include "include/spdlog/sinks/daily_file_sink.h"
#include "exceptions.h"
#include "platform.h"
#include "ImageWriter.h"
#include "CaptureStills.h"


const std::string						R_OK = "OK";
const std::string						R_NG = "NG";

enum ServerStatus{
	INITIALIZATION_ERROR = -1,
	INITIALIZING = 0,
	IDLE,
	PROCESSING,
};

enum ErrorCode {
	WARN_INITIALIZING = 900,
	ERROR_INITIALIZATION,
	WARN_PROCESSING = 910,
	ERROR_INVALID_PARAMS,
	ERROR_CAPTURE,
	ERROR_INVALID_REQUEST = 990,
	ERROR_UNKNOWN = 999,
};


std::string dump_headers(const httplib::Headers &headers) {
	std::string s;
	char buf[BUFSIZ];

	for (auto it = headers.begin(); it != headers.end(); ++it) {
		const auto &x = *it;
		snprintf(buf, sizeof(buf), "%s: %s\n", x.first.c_str(), x.second.c_str());
		s += buf;
	}

	return s;
}

std::string dump_req_and_res(const httplib::Request &req, const httplib::Response &res) {
	std::string s = "\n";
	char buf[BUFSIZ];

	snprintf(buf, sizeof(buf), "%s %s %s", req.method.c_str(),
		req.version.c_str(), req.path.c_str());
	s += buf;

	std::string query;
	for (auto it = req.params.begin(); it != req.params.end(); ++it) {
		const auto &x = *it;
		snprintf(buf, sizeof(buf), "%c%s=%s",
			(it == req.params.begin()) ? '?' : '&', x.first.c_str(),
			x.second.c_str());
		query += buf;
	}
	snprintf(buf, sizeof(buf), "%s\n", query.c_str());
	s += buf;

	//s += dump_headers(req.headers);

	if (!req.body.empty()) { s += req.body; }
	s += "\n";

	s += "--------------------------------\n";

	snprintf(buf, sizeof(buf), "%d %s\n", res.status, res.version.c_str());
	s += buf;
	//s += dump_headers(res.headers);

	if (!res.body.empty()) { s += res.body; }

	return s;
}

void validate_request_params(const std::string captureDirectory, const std::string filenamePrefix, const std::string imageFormat)
{
	// validate captureDirectory
	if (captureDirectory.empty())
	{
		throw InvalidParams("You must set a capture directory");
	}
	else if (!IsPathDirectory(captureDirectory))
	{
		throw InvalidParams("Invalid directory specified");
	}

	// validate filenamePrefix
	if (filenamePrefix.empty())
	{
		throw InvalidParams("You must set a filename prefix");
	}

	// validate imageFormat
	if (imageFormat.empty())
	{
		throw InvalidParams("You must set an image format");
	}
	else if (std::find(supportedImageFormats.begin(), supportedImageFormats.end(), imageFormat) == supportedImageFormats.end())
	{
		throw InvalidParams("Invalid image format specified '"+imageFormat+"'");
	}
}

std::string make_response(const std::string& result, const std::string& filepath="", const int& errCode=0, const std::string& errMsg="")
{
	Json::StreamWriterBuilder builder;
	std::string jsonString;
	Json::Value root;
	Json::Value body;

	if (result == R_OK) {
		if (!filepath.empty())
		{
			body["filepath"] = filepath;
		}
	}
	else
	{
		body["code"] = errCode;
		body["message"] = errMsg;
	}

	root["response"] = result;
	root["body"] = body;
	jsonString = Json::writeString(builder, root);
	return jsonString;
}

int main(int argc, char* argv[])
{
	std::string					usrCommand = "";
	// Configuration Flags
	int							deckLinkIndex = -1;
	int							displayModeIndex = -1;
	int							pixelFormatIndex = 0;
	int							portNo = -1;
	bool						enableFormatDetection = false;

	HRESULT						result;
	int							exitStatus = 1;
	int							idx;
	bool						supportsFormatDetection = false;

	std::thread					captureStillsThread;

	IDeckLinkIterator*			deckLinkIterator = NULL;
	IDeckLink*					deckLink = NULL;
	DeckLinkInputDevice*		selectedDeckLinkInput = NULL;

	BMDDisplayMode				selectedDisplayMode = bmdModeNTSC;
	std::string					selectedDisplayModeName;
	std::vector<std::string>	deckLinkDeviceNames;

	// HTTP Server
	httplib::Server 			svr;
	ServerStatus				serverStatus = INITIALIZING;
	std::string					initializationErrMsg = "initializing";

	// Logging
	int							logLevel = spdlog::level::debug;
	std::string					logDirectory = "";

	// Get command line options
	for (int i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "-d") == 0)
			deckLinkIndex = atoi(argv[++i]);

		else if (strcmp(argv[i], "-p") == 0)
			portNo = atoi(argv[++i]);

		else if (strcmp(argv[i], "-m") == 0)
			displayModeIndex = atoi(argv[++i]);

		else if (strcmp(argv[i], "-f") == 0)
			pixelFormatIndex = atoi(argv[++i]);

		else if (strcmp(argv[i], "--log-level") == 0)
			logLevel = atoi(argv[++i]);

		else if (strcmp(argv[i], "--log-dir") == 0)
			logDirectory = argv[++i];
	}

	// Initialize logger
	if ((logLevel < 0) || (logLevel >= spdlog::level::level_enum::n_levels))
	{
		fprintf(stderr, "You must select a log level between 0 - %d", spdlog::level::level_enum::n_levels - 1);
		return exitStatus;
	}
	spdlog::set_level(static_cast<spdlog::level::level_enum>(logLevel));
	spdlog::set_pattern("%Y-%m-%d %H:%M:%S [%-8l] [thread %-5t] %v");
	try
	{
		auto logger = spdlog::daily_logger_mt("SnapShotCreator", logDirectory+"\\"+"SnapShotCreator.log");
		spdlog::set_default_logger(logger);
	}
	catch (...)
	{
		fprintf(stderr, "Invalid log directory specified");
		return exitStatus;
	}

	// Print startup command
	for(int i = 0; i < argc; ++i)
	{
		usrCommand += argv[i];
		usrCommand += " ";
	}
	spdlog::info("Startup command:\n{}", usrCommand);

	// Basic check for commmand line options in order to start the server
	if (!svr.is_valid())
	{
		spdlog::error("Server initialization failed");
		return exitStatus;
	}
	if (deckLinkIndex < 0)
	{
		spdlog::error("You must select a device");
		return exitStatus;
	}
	if ((portNo > 65535) || (portNo < 2000))
	{
		spdlog::error("You must select a port number between 2000 - 65535");
		return exitStatus;
	}

	// Initialize input device
	// TODO(low): wrap in a function and make another thread for it.
	try
	{
		// Initialize COM on this thread
		result = CoInitializeEx(NULL, COINIT_MULTITHREADED);
		if (FAILED(result))
			throw InitializationError("Initialization of COM failed");

		result = ImageWriter::Initialize();
		if (FAILED(result))
			throw InitializationError("Initialization of ImageWriter failed");

		result = GetDeckLinkIterator(&deckLinkIterator);
		if (result != S_OK)
			throw InitializationError("Initialization of deckLinkIterator failed");

		// Obtain the required DeckLink device
		// TODO(low): move to CaptureStills.cpp
		idx = 0;

		while ((result = deckLinkIterator->Next(&deckLink)) == S_OK)
		{
			dlstring_t deckLinkName;

			result = deckLink->GetDisplayName(&deckLinkName);
			if (result == S_OK)
			{
				deckLinkDeviceNames.push_back(DlToStdString(deckLinkName));
				DeleteString(deckLinkName);
			}

			if (idx++ == deckLinkIndex)
			{
				// Check that selected device supports capture
				IDeckLinkProfileAttributes*	deckLinkAttributes = NULL;
				int64_t						ioSupportAttribute = 0;
				dlbool_t					formatDetectionSupportAttribute;

				result = deckLink->QueryInterface(IID_IDeckLinkProfileAttributes, (void**)&deckLinkAttributes);
				if (result != S_OK)
					throw InitializationError("Unable to get IDeckLinkAttributes interface");

				// Check whether device supports cpature
				result = deckLinkAttributes->GetInt(BMDDeckLinkVideoIOSupport, &ioSupportAttribute);
				if ((result != S_OK) || ((ioSupportAttribute & bmdDeviceSupportsCapture) == 0))
					throw InitializationError("Selected device does not support capture");

				// Check if input mode detection is supported.
				result = deckLinkAttributes->GetFlag(BMDDeckLinkSupportsInputFormatDetection, &formatDetectionSupportAttribute);
				supportsFormatDetection = (result == S_OK) && (formatDetectionSupportAttribute != FALSE);

				selectedDeckLinkInput = new DeckLinkInputDevice(deckLink);

				deckLinkAttributes->Release();
			}

			deckLink->Release();
		}

		// Get display modes from the selected decklink output
		// TODO(low): move to CaptureStills.cpp
		if (selectedDeckLinkInput == NULL)
			throw InitializationError("Invalid input device selected");

		result = selectedDeckLinkInput->Init();
		if (result != S_OK)
			throw InitializationError("Unable to initialize DeckLink input interface");

		// Get the display mode
		if ((displayModeIndex < -1) || (displayModeIndex >= (int)selectedDeckLinkInput->GetDisplayModeList().size()))
			throw InitializationError("You must select a valid display mode");

		if (displayModeIndex == -1)
		{
			if (!supportsFormatDetection)
				throw InitializationError("Format detection is not supported on this device");

			enableFormatDetection = true;

			// Format detection still needs a valid mode to start with
			selectedDisplayMode = bmdModeNTSC;
			selectedDisplayModeName = "Automatic mode detection";
			pixelFormatIndex = 0;
		}
		else if ((pixelFormatIndex < 0) || (pixelFormatIndex >= (int)kSupportedPixelFormats.size()))
		{
			throw InitializationError("You must select a valid pixel format");
		}
		else
		{
			dlbool_t				displayModeSupported;
			dlstring_t				displayModeNameStr;
			IDeckLinkDisplayMode*	displayMode = selectedDeckLinkInput->GetDisplayModeList()[displayModeIndex];

			result = displayMode->GetName(&displayModeNameStr);
			if (result == S_OK)
			{
				selectedDisplayModeName = DlToStdString(displayModeNameStr);
				DeleteString(displayModeNameStr);
			}

			selectedDisplayMode = displayMode->GetDisplayMode();

			// Check display mode is supported with given options
			result = selectedDeckLinkInput->GetDeckLinkInput()->DoesSupportVideoMode(bmdVideoConnectionUnspecified,
				selectedDisplayMode,
				std::get<kPixelFormatValue>(kSupportedPixelFormats[pixelFormatIndex]),
				bmdNoVideoInputConversion,
				bmdSupportedVideoModeDefault,
				NULL,
				&displayModeSupported);
			if ((result != S_OK) || (!displayModeSupported))
			{
				throw InitializationError("Display mode "+selectedDisplayModeName+" with pixel format "+std::get<kPixelFormatString>(kSupportedPixelFormats[pixelFormatIndex])+" is not supported by device");
			}
		}

		// Try capturing
		result = selectedDeckLinkInput->StartCapture(selectedDisplayMode, std::get<kPixelFormatValue>(kSupportedPixelFormats[pixelFormatIndex]), enableFormatDetection);
		if (result != S_OK)
			throw InitializationError("Failed to start capture");

		// Print the selected configuration
		spdlog::info("Capturing with the following configuration:\n"
			" - Capture device: {}\n"
			" - Video mode: {}\n"
			" - Pixel format: {}",
			selectedDeckLinkInput->GetDeviceName().c_str(),
			selectedDisplayModeName.c_str(),
			std::get<kPixelFormatString>(kSupportedPixelFormats[pixelFormatIndex]).c_str()
		);

		// Wait for incoming video frame
		IDeckLinkVideoFrame*		receivedVideoFrame = NULL;
		bool 						captureCancelled;
		if (!selectedDeckLinkInput->WaitForVideoFrameArrived(&receivedVideoFrame, captureCancelled))
		{
			throw InitializationError("Timeout waiting for valid frame");
		}

		// Stop capturing on successful try
		selectedDeckLinkInput->StopCapture();

		// Update server status
		serverStatus = IDLE;
		spdlog::debug("System all green");
	}
	catch (std::exception& ex)
	{
		// Update server status and print error
		serverStatus = INITIALIZATION_ERROR;
		initializationErrMsg = ex.what();
		spdlog::error(initializationErrMsg);
		CaptureStills::DisplayUsage(selectedDeckLinkInput, deckLinkDeviceNames, deckLinkIndex, displayModeIndex, supportsFormatDetection);

		// free resources
		if (selectedDeckLinkInput != NULL)
		{
			selectedDeckLinkInput->CancelCapture();
			selectedDeckLinkInput->Release();
			selectedDeckLinkInput = NULL;
		}
		if (deckLinkIterator != NULL)
		{
			deckLinkIterator->Release();
			deckLinkIterator = NULL;
		}
		ImageWriter::UnInitialize();
		CoUninitialize();
	}

	// create a snapshot
	svr.Post("/", [&](const httplib::Request &req, httplib::Response &res) {
		std::string 							rawJson = req.body;
		Json::Value 							root;
		Json::CharReaderBuilder 				jsonBuilder;
		const std::unique_ptr<Json::CharReader>	jsonReader(jsonBuilder.newCharReader());

		std::string								command;
		std::string 							captureDirectory;
		std::string 							filenamePrefix;
		std::string 							imageFormat;
		std::string 							filepath;
		std::string 							err;

		// Parse JSON body
		if (!jsonReader->parse(rawJson.c_str(), rawJson.c_str() + rawJson.length(), &root, &err))
		{
			throw InvalidRequest("failed to parse JSON body");
		}

		// Get command
		command = root["command"].asString();

		if (command == "IS_INITIALIZED")
		{
			// Confirm if server is ready
			if (serverStatus > INITIALIZING)
			{
				res.set_content(make_response(R_OK), "application/json");
			}
			else
			{
				throw InitializationError(initializationErrMsg);
			}
		}

		else if (command == "SHUTDOWN")
		{
			// Cancel capture and shutdown server
			if (selectedDeckLinkInput != NULL)
			{
				selectedDeckLinkInput->CancelCapture();
				selectedDeckLinkInput->Release();
				selectedDeckLinkInput = NULL;
			}
			if (deckLinkIterator != NULL)
			{
				deckLinkIterator->Release();
				deckLinkIterator = NULL;
			}
			ImageWriter::UnInitialize();
			CoUninitialize();
			res.set_content(make_response(R_OK), "application/json");
			svr.stop();
		}

		else if (command == "CREATE_SNAPSHOT")
		{
			if (serverStatus < IDLE)
			{
				throw InitializationError(initializationErrMsg);
			}
			else if (serverStatus > IDLE)
			{
				throw CaptureError("server is processing another snapshot");
			}

			// Create Snapshot
			// Update server status
			serverStatus = PROCESSING;

			// Get parameters
			captureDirectory = root["data"].get("output_directory", "").asCString();
			filenamePrefix = root["data"].get("filename_prefix", "").asCString();
			imageFormat = root["data"].get("image_format", "").asCString();

			// Print the request params
			spdlog::info("Capturing snapshot:\n"
				" - Capture directory: {}\n"
				" - Filename prefix: {}\n"
				" - Image format: {}",
				captureDirectory.c_str(),
				filenamePrefix.c_str(),
				imageFormat.c_str()
			);

			// Validate params
			validate_request_params(captureDirectory, filenamePrefix, imageFormat);

			// Start capturing
			result = selectedDeckLinkInput->StartCapture(selectedDisplayMode, std::get<kPixelFormatValue>(kSupportedPixelFormats[pixelFormatIndex]), enableFormatDetection);
			if (result != S_OK)
				throw CaptureError("failed to start capture");

			// Start thread for capture processing
			captureStillsThread = std::thread([&] {
				CaptureStills::CreateSnapshot(selectedDeckLinkInput, captureDirectory, filenamePrefix, imageFormat, filepath, err);
			});
			// Wait on return of main capture stills thread
			captureStillsThread.join();
			// Stop capturing
			selectedDeckLinkInput->StopCapture();

			// Update server status
			serverStatus = IDLE;

			// Check result
			if (!err.empty())
			{
				throw CaptureError(err);
			}
			res.set_content(make_response(R_OK, filepath), "application/json");
		}
		else
		{
			throw InvalidRequest("'command' key not found or not supported '"+command+"'");
		}

	});

	// if server error occures, return with response
	svr.set_error_handler([&](const httplib::Request & /*req*/, httplib::Response &res) {
		int				errCode;
		std::string 	errMsg = res.get_header_value("EXCEPTION_WHAT");
		std::string 	errType = res.get_header_value("EXCEPTION_TYPE");

		if (errType == "class InitializationError")
		{
			if (serverStatus == INITIALIZING)
			{
				res.status = 200;
				errCode = WARN_INITIALIZING;
				spdlog::warn(errMsg);
			}
			else
			{
				res.status = 500;
				errCode = ERROR_INITIALIZATION;
				errMsg = "InitializationError: " + errMsg;
				spdlog::error(errMsg);
			}
		}
		else if (errType == "class InvalidParams")
		{
			res.status = 400;
			errCode = ERROR_INVALID_PARAMS;
			errMsg = "InvalidParams: " + errMsg;
			spdlog::warn(errMsg);
			serverStatus = IDLE;
		}
		else if (errType == "class CaptureError")
		{
			if (serverStatus == PROCESSING)
			{
				res.status = 200;
				errCode = WARN_PROCESSING;
				spdlog::warn(errMsg);
			}
			else
			{
				res.status = 500;
				errCode = ERROR_CAPTURE;
				errMsg = "CaptureError: " + errMsg;
				spdlog::error(errMsg);
				serverStatus = IDLE;
			}
		}
		else if (errType == "class InvalidRequest" || errType.empty())
		{
			res.status = 400;
			errCode = ERROR_INVALID_REQUEST;
			if (errType.empty())
				errMsg = "URL error";
			errMsg = "InvalidRequest: " + errMsg;
			spdlog::warn(errMsg);
			serverStatus = IDLE;
		}
		else
		{
			res.status = 500;
			errCode = ERROR_UNKNOWN;
			errMsg = "UnknownError: " + errMsg;
			spdlog::error(errMsg);
			serverStatus = IDLE;
		}
		res.set_content(make_response(R_NG, "", errCode, errMsg), "application/json");
	});

	// set logger for request/response
	svr.set_logger([&](const httplib::Request &req, const httplib::Response &res) {
		spdlog::debug(dump_req_and_res(req, res).c_str());
	});

	spdlog::info("Server started at http://localhost:{}", portNo);
	svr.listen("localhost", portNo);

	// All Okay.
	exitStatus = 0;
}