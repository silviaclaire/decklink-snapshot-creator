#pragma once

#include <list>
#include <tuple>
#include <vector>

#include "DeckLinkInputDevice.h"

// Pixel format tuple encoding {BMDPixelFormat enum, Pixel format display name}
const std::vector<std::tuple<BMDPixelFormat, std::string>> kSupportedPixelFormats
{
	std::make_tuple(bmdFormat8BitYUV, "8 bit YUV (4:2:2)"),
	std::make_tuple(bmdFormat10BitYUV, "10 bit YUV (4:2:2)"),
	std::make_tuple(bmdFormat8BitARGB, "8 bit ARGB (4:4:4)"),
	std::make_tuple(bmdFormat8BitBGRA, "8 bit BGRA (4:4:4)"),
	std::make_tuple(bmdFormat10BitRGB, "10 bit RGB (4:4:4)"),
	std::make_tuple(bmdFormat12BitRGB, "12 bit RGB (4:4:4)"),
	std::make_tuple(bmdFormat12BitRGBLE, "12 bit RGB (4:4:4) Little-Endian"),
	std::make_tuple(bmdFormat10BitRGBX, "10 bit RGBX (4:4:4)"),
	std::make_tuple(bmdFormat10BitRGBXLE, "10 bit RGBX (4:4:4) Little-Endian"),
};
enum {
	kPixelFormatValue = 0,
	kPixelFormatString
};

// Supported image format list
const std::list<std::string> supportedImageFormats = {
	"bmp",
	"png",
	"tiff",
	"jpeg",
};

namespace CaptureStills
{
	HRESULT GetDeckLinkInputDevice(const int deckLinkIndex, DeckLinkInputDevice* selectedDeckLinkInput,
								   std::vector<std::string>& deckLinkDeviceNames, bool& supportsFormatDetection);
	void DisplayUsage(DeckLinkInputDevice* selectedDeckLinkInput, const std::vector<std::string>& deviceNames,
		const int selectedDeviceIndex, const int selectedDisplayModeIndex, const bool supportsFormatDetection);
	void CreateSnapshot(DeckLinkInputDevice* deckLinkInput, const int captureInterval, const int framesToCapture,
		const std::string& captureDirectory, const std::string& filenamePrefix, const std::string& imageFormat);
}
