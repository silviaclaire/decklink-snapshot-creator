#include <httplib.h>
#include <Windows.h>
#include <stdio.h>
#include <json/json.h>

#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "platform.h"
#include "ImageWriter.h"
#include "CaptureStills.h"

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

std::string log(const httplib::Request &req, const httplib::Response &res) {
	std::string s;
	char buf[BUFSIZ];

	s += "--------------------------------\n";

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

	s += "--------------------------------\n";

	snprintf(buf, sizeof(buf), "%d %s\n", res.status, res.version.c_str());
	s += buf;
	s += dump_headers(res.headers);
	s += "\n";

	if (!res.body.empty()) { s += res.body; }

	s += "\n";

	return s;
}

bool validateRequestParams(const std::string captureDirectory, const std::string filenamePrefix, const std::string imageFormat)
{
	// validate captureDirectory
	if (captureDirectory.empty())
	{
		fprintf(stderr, "You must set a capture directory\n");
		return false;
	}
	else if (!IsPathDirectory(captureDirectory))
	{
		fprintf(stderr, "Invalid directory specified:%s\n", captureDirectory.c_str());
		return false;
	}

	// validate filenamePrefix
	if (filenamePrefix.empty())
	{
		fprintf(stderr, "You must set a filename prefix\n");
		return false;
	}

	// validate imageFormat
	if (imageFormat.empty())
	{
		fprintf(stderr, "You must set an image format\n");
		return false;
	}
	else if (std::find(supportedImageFormats.begin(), supportedImageFormats.end(), imageFormat) == supportedImageFormats.end())
	{
		fprintf(stderr, "Invalid image format specified:%s\n", imageFormat.c_str());
		return false;
	}
	return true;
}


int main(int argc, char* argv[])
{
	// Configuration Flags
	bool						displayHelp = false;
	int							deckLinkIndex = -1;
	int							displayModeIndex = -2;
	int							framesToCapture = 1;
	int							captureInterval = 1;
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

	// Initialize server
	httplib::Server 			svr;
	if (!svr.is_valid()) {
		fprintf(stderr, "Server initialization failed.\n");
		return 1;
	}

	// Initialize COM on this thread
	result = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	if (FAILED(result))
	{
		fprintf(stderr, "Initialization of COM failed - result = %08x.\n", result);
		return 1;
	}

	result = ImageWriter::Initialize();
	if (FAILED(result))
		goto bail;

	result = GetDeckLinkIterator(&deckLinkIterator);
	if (result != S_OK)
		goto bail;

	for (int i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "-d") == 0)
			deckLinkIndex = atoi(argv[++i]);

		else if (strcmp(argv[i], "-m") == 0)
			displayModeIndex = atoi(argv[++i]);

		else if (strcmp(argv[i], "-i") == 0)
			captureInterval = atoi(argv[++i]);

		else if (strcmp(argv[i], "-n") == 0)
			framesToCapture = atoi(argv[++i]);

		else if (strcmp(argv[i], "-f") == 0)
			pixelFormatIndex = atoi(argv[++i]);

		else if (strcmp(argv[i], "-p") == 0)
			portNo = atoi(argv[++i]);

		else if ((strcmp(argv[i], "?") == 0) || (strcmp(argv[i], "-h") == 0))
			displayHelp = true;
	}

	if (deckLinkIndex < 0)
	{
		fprintf(stderr, "You must select a device\n");
		displayHelp = true;
	}

	if ((portNo > 65535) || (portNo < 2000))
	{
		fprintf(stderr, "You must select a port number between 2000 - 65535\n");
		displayHelp = true;
	}

	// Obtain the required DeckLink device
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
			{
				fprintf(stderr, "Unable to get IDeckLinkAttributes interface\n");
				goto bail;
			}

			// Check whether device supports cpature
			result = deckLinkAttributes->GetInt(BMDDeckLinkVideoIOSupport, &ioSupportAttribute);

			if ((result != S_OK) || ((ioSupportAttribute & bmdDeviceSupportsCapture) == 0))
			{
				fprintf(stderr, "Selected device does not support capture\n");
				displayHelp = true;
			}
			else
			{
				// Check if input mode detection is supported.
				result = deckLinkAttributes->GetFlag(BMDDeckLinkSupportsInputFormatDetection, &formatDetectionSupportAttribute);
				supportsFormatDetection = (result == S_OK) && (formatDetectionSupportAttribute != FALSE);

				selectedDeckLinkInput = new DeckLinkInputDevice(deckLink);
			}

			deckLinkAttributes->Release();
		}

		deckLink->Release();
	}

	// Get display modes from the selected decklink output
	if (selectedDeckLinkInput != NULL)
	{
		result = selectedDeckLinkInput->Init();
		if (result != S_OK)
		{
			fprintf(stderr, "Unable to initialize DeckLink input interface");
			goto bail;
		}

		// Get the display mode
		if ((displayModeIndex < -1) || (displayModeIndex >= (int)selectedDeckLinkInput->GetDisplayModeList().size()))
		{
			fprintf(stderr, "You must select a valid display mode\n");
			displayHelp = true;
		}
		else if (displayModeIndex == -1)
		{
			if (!supportsFormatDetection)
			{
				fprintf(stderr, "Format detection is not supported on this device\n");
				displayHelp = true;
			}
			else
			{
				enableFormatDetection = true;

				// Format detection still needs a valid mode to start with
				selectedDisplayMode = bmdModeNTSC;
				selectedDisplayModeName = "Automatic mode detection";
				pixelFormatIndex = 0;
			}
		}
		else if ((pixelFormatIndex < 0) || (pixelFormatIndex >= (int)kSupportedPixelFormats.size()))
		{
			fprintf(stderr, "You must select a valid pixel format\n");
			displayHelp = true;
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
				fprintf(stderr, "Display mode %s with pixel format %s is not supported by device\n",
					selectedDisplayModeName.c_str(),
					std::get<kPixelFormatString>(kSupportedPixelFormats[pixelFormatIndex]).c_str()
				);
				displayHelp = true;
			}
		}
	}
	else
	{
		fprintf(stderr, "Invalid input device selected\n");
		displayHelp = true;
	}

	if (displayHelp)
	{
		CaptureStills::DisplayUsage(selectedDeckLinkInput, deckLinkDeviceNames, deckLinkIndex, displayModeIndex, supportsFormatDetection);
		goto bail;
	}

	// Try capturing
	result = selectedDeckLinkInput->StartCapture(selectedDisplayMode, std::get<kPixelFormatValue>(kSupportedPixelFormats[pixelFormatIndex]), enableFormatDetection);
	if (result != S_OK)
		goto bail;

	// Print the selected configuration
	fprintf(stderr, "Capturing with the following configuration:\n"
		" - Capture device: %s\n"
		" - Video mode: %s\n"
		" - Pixel format: %s\n"
		" - Frames to capture: %d\n"
		" - Capture interval: %d\n",
		selectedDeckLinkInput->GetDeviceName().c_str(),
		selectedDisplayModeName.c_str(),
		std::get<kPixelFormatString>(kSupportedPixelFormats[pixelFormatIndex]).c_str(),
		framesToCapture,
		captureInterval
	);
	// Stop capturing on successful try
	selectedDeckLinkInput->StopCapture();
	fprintf(stderr, "System all green.\n");

	// create a snapshot
	svr.Post("/", [&](const httplib::Request &req, httplib::Response &res) {
		std::string 							rawJson;
		Json::Value 							root;
		Json::CharReaderBuilder 				jsonBuilder;
		const std::unique_ptr<Json::CharReader>	jsonReader(jsonBuilder.newCharReader());
		JSONCPP_STRING 							err;

		std::string 							captureDirectory;
		std::string 							filenamePrefix;
		std::string 							imageFormat;

		fprintf(stderr, "================================\n");
		rawJson = req.body;
		fprintf(stderr, "%s\n", rawJson.c_str());

		// Parse JSON body
		bool isParsable = jsonReader->parse(rawJson.c_str(), rawJson.c_str() + rawJson.length(), &root, &err);
		if (!isParsable)
		{
			char *fmt = R"("{"result":"ERROR", "message":"Failed to parse JSON body:%s"}")";
			char buf[BUFSIZ];
			snprintf(buf, sizeof(buf), fmt, err.c_str());
			res.set_content(buf, "application/json");
		}
		else
		{
			// Get parameters
			captureDirectory = root["data"].get("outputDirectory", "").asCString();
			filenamePrefix = root["data"].get("filenamePrefix", "").asCString();
			imageFormat = root["data"].get("imageFormat", "").asCString();

			if (!validateRequestParams(captureDirectory, filenamePrefix, imageFormat))
			{
				res.set_content("{\"result\":\"Invalid request\"}", "application/json");
			}
			else
			{
				// Print the request params
				fprintf(stderr, "Capturing snapshot:\n"
					" - Capture directory: %s\n"
					" - Filename prefix: %s\n"
					" - Image format: %s\n",
					captureDirectory.c_str(),
					filenamePrefix.c_str(),
					imageFormat.c_str()
				);

				// Start capturing
				result = selectedDeckLinkInput->StartCapture(selectedDisplayMode, std::get<kPixelFormatValue>(kSupportedPixelFormats[pixelFormatIndex]), enableFormatDetection);
				if (result != S_OK)
				{
					res.set_content("{\"result\":\"Failed to start capture\"}", "application/json");
				}
				else
				{
					// Start thread for capture processing
					captureStillsThread = std::thread([&] {
						CaptureStills::CreateSnapshot(selectedDeckLinkInput, captureInterval, framesToCapture, captureDirectory, filenamePrefix, imageFormat);
					});
					// Wait on return of main capture stills thread
					captureStillsThread.join();
					selectedDeckLinkInput->StopCapture();
					res.set_content("{\"result\":\"Completed\"}", "application/json");
				}
			}
		}
	});

	// stop server
	svr.Get("/stop", [&](const httplib::Request& req, httplib::Response& res) {
		selectedDeckLinkInput->CancelCapture();
		res.set_content("{\"result\":\"Capture canceled. Stopping server...\"}", "application/json");
		svr.stop();
	});

	// if server error occures, return with response
	svr.set_error_handler([](const httplib::Request & /*req*/, httplib::Response &res) {
		const char *fmt = "{\"result\":\"ERROR\", \"status\":%d}";
		char buf[BUFSIZ];
		snprintf(buf, sizeof(buf), fmt, res.status);
		res.set_content(buf, "application/json");
	});

	// set logger for request/response
	svr.set_logger([](const httplib::Request &req, const httplib::Response &res) {
		printf("%s", log(req, res).c_str());
	});

	fprintf(stderr, "Server started at port %d.\n", portNo);
	svr.listen("localhost", portNo);

	// All Okay.
	exitStatus = 0;

bail:
	if (selectedDeckLinkInput != NULL)
	{
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

	return exitStatus;
}
