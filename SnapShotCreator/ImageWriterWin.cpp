/* -LICENSE-START-
** Copyright (c) 2018 Blackmagic Design
**
** Permission is hereby granted, free of charge, to any person or organization
** obtaining a copy of the software and accompanying documentation covered by
** this license (the "Software") to use, reproduce, display, distribute,
** execute, and transmit the Software, and to prepare derivative works of the
** Software, and to permit third-parties to whom the Software is furnished to
** do so, all subject to the following:
**
** The copyright notices in the Software and this entire statement, including
** the above license grant, this restriction and the following disclaimer,
** must be included in all copies of the Software, in whole or in part, and
** all derivative works of the Software, unless such copies or derivative
** works are solely in the form of machine-executable object code generated by
** a source language processor.
**
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
** IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
** FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
** SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
** FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
** ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
** DEALINGS IN THE SOFTWARE.
** -LICENSE-END-
*/

#include <wincodec.h>		// For handing bitmap files
#include <atlstr.h>
#include <algorithm>

#include "include/spdlog/spdlog.h"
#include "utils.h"
#include "ImageWriter.h"

namespace ImageWriter
{
	IWICImagingFactory*	g_wicFactory;
}

HRESULT ImageWriter::Initialize()
{
	// Create WIC Imaging factory to write image stills
	HRESULT result = CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_wicFactory));
	if (FAILED(result))
	{
		spdlog::error("A WIC imaging factory could not be created");
	}

	return result;
}

HRESULT ImageWriter::UnInitialize()
{
	if (g_wicFactory != NULL)
	{
		g_wicFactory->Release();
		g_wicFactory = NULL;
	}
	return S_OK;
}

std::string ImageWriter::GetFilepath(const std::string& path, const std::string& prefix, const std::string& extension)
{
	const char*					fmt = "%Y%m%d%H%M%S";

	std::string filepath = path + "\\" + prefix + CurrentDateTime(fmt) + "." + extension;
	return filepath;
}

HRESULT ImageWriter::WriteVideoFrameToImage(IDeckLinkVideoFrame* videoFrame, const std::string& imgFilename, const std::string& imageFormat)
{
	HRESULT								result = S_OK;
	void*								frameBytes = NULL;

	IWICBitmapEncoder*					bitmapEncoder = NULL;
	IWICBitmapFrameEncode*				bitmapFrame = NULL;
	IWICStream*							fileStream = NULL;
	WICPixelFormatGUID					pixelFormat;

	CString filename = imgFilename.c_str();

	// Ensure video frame has expected pixel format
	if (videoFrame->GetPixelFormat() != bmdFormat8BitBGRA)
	{
		spdlog::error("Video frame is not in 8-Bit BGRA pixel format");
		return E_FAIL;
	}

	videoFrame->GetBytes(&frameBytes);
	if (frameBytes == NULL)
	{
		spdlog::error("Could not get DeckLinkVideoFrame buffer pointer");
		result = E_OUTOFMEMORY;
		goto bail;
	}

	result = g_wicFactory->CreateStream(&fileStream);
	if (FAILED(result))
		goto bail;

	result = fileStream->InitializeFromFilename(filename, GENERIC_WRITE);
	if (FAILED(result))
		goto bail;

	if (imageFormat == "bmp")
	{
		result = g_wicFactory->CreateEncoder(GUID_ContainerFormatBmp, NULL, &bitmapEncoder);
		pixelFormat = GUID_WICPixelFormat32bppBGR;
	}
	else if (imageFormat == "png")
	{
		result = g_wicFactory->CreateEncoder(GUID_ContainerFormatPng, NULL, &bitmapEncoder);
		pixelFormat = GUID_WICPixelFormat32bppBGRA;
	}
	else if (imageFormat == "tiff")
	{
		result = g_wicFactory->CreateEncoder(GUID_ContainerFormatTiff, NULL, &bitmapEncoder);
		pixelFormat = GUID_WICPixelFormat32bppBGRA;
	}
	else if (imageFormat == "jpeg")
	{
		result = g_wicFactory->CreateEncoder(GUID_ContainerFormatJpeg, NULL, &bitmapEncoder);
		pixelFormat = GUID_WICPixelFormat24bppBGR;
	}

	if (FAILED(result))
		goto bail;

	result = bitmapEncoder->Initialize(fileStream, WICBitmapEncoderNoCache);
	if (FAILED(result))
		goto bail;

	result = bitmapEncoder->CreateNewFrame(&bitmapFrame, NULL);
	if (FAILED(result))
		goto bail;

	result = bitmapFrame->Initialize(NULL);
	if (FAILED(result))
		goto bail;

	// Set bitmap frame size based on video frame
	result = bitmapFrame->SetSize(videoFrame->GetWidth(), videoFrame->GetHeight());
	if (FAILED(result))
		goto bail;

	// Bitmap pixel format will match video frame
	result = bitmapFrame->SetPixelFormat(&pixelFormat);
	if (FAILED(result))
		goto bail;

	// Write video buffer to bitmap
	result = bitmapFrame->WritePixels(videoFrame->GetHeight(), videoFrame->GetRowBytes(), videoFrame->GetHeight()*videoFrame->GetRowBytes(), (BYTE*)frameBytes);
	if (FAILED(result))
		goto bail;

	result = bitmapFrame->Commit();
	if (FAILED(result))
		goto bail;

	result = bitmapEncoder->Commit();
	if (FAILED(result))
		goto bail;

bail:
	if (bitmapFrame != NULL)
		bitmapFrame->Release();

	if (bitmapEncoder != NULL)
		bitmapEncoder->Release();

	if (fileStream != NULL)
		fileStream->Release();

	return result;
}
