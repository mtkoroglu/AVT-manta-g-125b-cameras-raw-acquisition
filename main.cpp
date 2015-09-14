#define _CRT_SECURE_NO_DEPRECATE
#include "PvApi.h" // This is the header file provided by AVT company. Note that Vimba SDK is AVT's recommended future API. All credit goes to Young-Jin.
#include "Windows.h" // Sleep(), GetTickCount(), DWORD data type.
#include "opencv/cv.h" // this for C API of OpenCV.
#include "opencv2/highgui/highgui.hpp"
#include <ppl.h> // Parallel patterns library, concurrency namespace. Young-Jin uses this, so do I.
#include <string>
#include <iostream>
/* I borrow following code fragment from "Mastering OpenCV w/ Practical CV Projects" book, Chapter 1 project. Thanks to Shervin Emami who is an amazing guy in terms of
real-time implementations. He is a co-author of Mastering OpenCV with Practical CV Projects. By the way, Roy is author of Chapter 4, that is about SFM, and he uses
undistortPoints() and triangulatePoints() built-in functions of calib3d module sequentially in real-time. */
#if !defined VK_ESCAPE // VK stands for Virtual Key
#define VK_ESCAPE 0x1B // ASCII code for ESC character is 27
#endif
#define VK_CAPTURE_IMAGE 0x66 // 102 is ASCII code for 'f' so press 'f' while you are on imshow() display in order to capture images.
using namespace std;
using namespace cv; // OpenCV namespace. Required for keywords imported from OpenCV libraries.

unsigned long nCams = 2; // Number of Manta G-125 cameras. We need to use ethernet switch in case of nCams > 1.
bool displayImagesOnScreen = true;

typedef struct // This struct is associated with PvApi.h.
{
	unsigned long UID;
	tPvHandle Handle;
	tPvFrame Frame;
	tPvUint32 Counter;
	char Filename[20];
}tCamera;

void image_stream(); // Image acquisition in real-time.

int main(int argc, char* argv[])
{
	image_stream();
	return EXIT_SUCCESS;
}

void image_stream()
{
	int showingratio = 3; // this parameters resizes image when displaying image stream. greater values yield to smaller display.
	unsigned long i;
	int IsPacketOK = 1; // this variable is for checking if image pair is obtained or not from cameras
	unsigned long nFrames = 100000000;
	DWORD time_start, time_end; // these variables are used for computing fps and avg_fps.
	DWORD time_start_saving, time_end_saving;
	double elapsedtime_saving;
	double fps = 1.0; // frame per second
	double sum_fps(0);
	double avg_fps(0); // average fps

	unsigned long StreamBytesPerSecondPerCamera; // StreamBytesPerSecond
	unsigned long ExposureValue;
	// StreamBytesPerSecond (MegaBytes per second)
	double NetworkStreamMegaBytesPerSecond = 90; //	double NetworkStreamMegaBytesPerSecond = 60;
	// Camera exposure time [sec] (Auto: ExporeTimeInSecond = -1; manual-> auto; needs to restart cameras)
	//double ExposureTimeInSecond = 0.01; // this value is inherited from Young-Jin
	double ExposureTimeInSecond = 0.003; // as this variable decreases, fps rate increases
	// Calculating Stream bytes per seconds 1292 964
	// StreamBytesPerSecondPerCamera = (unsigned long)(NetworkStreamMegaBytesPerSecond*1000000.0/(double)nCams);
	StreamBytesPerSecondPerCamera = (unsigned long)(NetworkStreamMegaBytesPerSecond * 1024 * 1024 / (double)nCams);
	// Calculating ExposureValue
	ExposureValue = (unsigned long)(ExposureTimeInSecond * 1000000);

	tPvErr errCode;

	tCamera* Camera;
	Camera = new tCamera[nCams];

	for (i = 0; i<nCams; i++)
		memset(&Camera[i], 0, sizeof(tCamera));

	//====================================================================================================================================================
	//========================================================== Initialize the PvAPI ====================================================================
	//====================================================================================================================================================

	PvInitialize();

	//====================================================================================================================================================
	//============================================================ Wait for cameras ======================================================================
	//====================================================================================================================================================

	printf("Waiting for a camera");

	while (PvCameraCount()<nCams)
	{
		printf(".");
		Sleep(100); // wait for 100ms. Sleep() built-in fcn is defined in windows.h. use usleep() fcn in linux.
	}
	printf("\n\n");

	//====================================================================================================================================================
	//============================================================ Getting cameras =======================================================================
	//====================================================================================================================================================

	tPvCameraInfoEx* list = new tPvCameraInfoEx[nCams];

	unsigned long	numCameras;
	tPvUint32    connected;

	numCameras = PvCameraListEx(list, nCams, &connected, sizeof(tPvCameraInfoEx));

	printf("Number of detected camera: %d\n\n", numCameras);

	for (i = 0; i<numCameras; i++)
		printf("%s [ID %u]\n", list[i].SerialNumber, list[i].UniqueId);

	printf("\n");

	int selcount = 0;
	if (numCameras>0)
	{
		for (i = 0; i<numCameras; i++)
		{
			Camera[selcount].UID = list[selcount].UniqueId;
			printf("Selected camera%d: %s\n", selcount, list[selcount].SerialNumber);
			selcount++;
		}
		printf("\n");
	}

	//============================================================== Opening cameras ======================================================================================
	for (i = 0; i<numCameras; i++)
	{
		printf("Opening camera%d: ", i);
		if ((errCode = PvCameraOpen(Camera[i].UID, ePvAccessMaster, &(Camera[i].Handle))) != ePvErrSuccess)
		{
			if (errCode == ePvErrAccessDenied)
				printf("PvCameraOpen returned ePvErrAccessDenied:\nCamera already open as Master, or camera wasn't properly closed and still waiting to HeartbeatTimeout.");
			else
				printf("PvCameraOpen err: %u\n", errCode);
			// return false;
			exit(EXIT_FAILURE);
		}
		else
			printf("success\n");
	}
	//=====================================================================================================================================================================

	// Calculate frame buffer size & allocate image buffer
	unsigned long FrameSize = 0;
	for (i = 0; i<numCameras; i++)
	{
		PvAttrUint32Get(Camera[i].Handle, "TotalBytesPerFrame", &FrameSize);
		Camera[i].Frame.ImageBuffer = new char[FrameSize];
		Camera[i].Frame.ImageBufferSize = FrameSize;
	}

	//=======================================================================================================================================================
	//=============================================================== Image acquisition =====================================================================
	//=======================================================================================================================================================

	for (i = 0; i<numCameras; i++)
	{
		PvCaptureAdjustPacketSize(Camera[i].Handle, 8228);
		PvCaptureStart(Camera[i].Handle);
		PvCaptureQueueFrame(Camera[i].Handle, &(Camera[i].Frame), NULL);
		PvAttrUint32Set(Camera[i].Handle, "StreamBytesPerSecond", StreamBytesPerSecondPerCamera);
		PvAttrEnumSet(Camera[i].Handle, "PixelFormat", "Mono8");
		//	PvAttrEnumSet(Camera[i].Handle, "ExposureMode", "Auto");
		PvAttrEnumSet(Camera[i].Handle, "ExposureMode", "Manual");
		PvAttrUint32Set(Camera[i].Handle, "ExposureValue", ExposureValue);
		PvAttrEnumSet(Camera[i].Handle, "GainMode", "Auto");
		//	PvAttrEnumSet(Camera[i].Handle, "GainMode", "AutoOnce");
		PvAttrEnumSet(Camera[i].Handle, "FrameStartTriggerMode", "Software");
		PvAttrEnumSet(Camera[i].Handle, "AcquisitionMode", "Continuous");
		PvCommandRun(Camera[i].Handle, "AcquisitionStart");
	}

	unsigned long nSavedFrames = 1000;

	IplImage** img_ori; // IplImage* img = cvLoadImage("name.type")
	img_ori = new IplImage*[numCameras]; // img_ori is similar to img above.

	IplImage** img_sml; // img_sml is for displaying image stream.
	img_sml = new IplImage*[numCameras];

	for (i = 0; i<numCameras; i++) // creating monochrome images.
	{
		img_ori[i] = cvCreateImage(cvSize(Camera[i].Frame.Width, Camera[i].Frame.Height), 8, 1);
		img_sml[i] = cvCreateImage(cvSize(img_ori[i]->width / showingratio, img_ori[i]->height / showingratio), 8, 1);
	}

	// Images for showing. Width of this image is 2*w
	IplImage* img_shw_big = cvCreateImage(cvSize(img_sml[0]->width * 2, img_sml[0]->height), IPL_DEPTH_8U, 1);
	IplImage* img_shw_big_color = cvCreateImage(cvSize(img_sml[0]->width * 2, img_sml[0]->height), IPL_DEPTH_8U, 3); // BGR image for color purposes

	// cvRect for copying images to a big image to show
	CvRect* rtShwBig = new CvRect[numCameras];
	for (i = 0; i<numCameras; i++)
		rtShwBig[i] = cvRect(0, 0, img_sml[0]->width, img_sml[0]->height);

	// Setting values for cvRect
	if (numCameras>0)
	{
		rtShwBig[0].x = 0;
		rtShwBig[0].y = 0;
	}
	if (numCameras>1)
	{
		rtShwBig[1].x = img_sml[0]->width;
		rtShwBig[1].y = 0;
	}
	if (numCameras>2)
	{
		rtShwBig[2].x = 0;
		rtShwBig[2].y = img_sml[0]->height;
	}
	if (numCameras>3)
	{
		rtShwBig[3].x = img_sml[0]->width;
		rtShwBig[3].y = img_sml[0]->height;
	}

	int Sizeof_SavedImageFileName = 50; //int Sizeof_SavedImageFileNameOriginal = 50;
	char** SavedImageFileName = new char*[numCameras]; //char** SavedImageFileNameOriginal = new char*[numCameras];
	for (i = 0; i < numCameras; i++)
	{
		SavedImageFileName[i] = new char[Sizeof_SavedImageFileName];
		//SavedImageFileNameOriginal[i] = new char[Sizeof_SavedImageFileNameOriginal];
	}

	unsigned long nCount = 0;
	int c;

	//char WindowName[20];
	const char* WindowName = "Capture Window";
	//sprintf_s(WindowName, "Capture Window"); // this line was Young-Jin's line
	if (displayImagesOnScreen)
		cvNamedWindow(WindowName, 1);

	printf("\nCamera is running.\nPress ESC to stop, spacebar to start capturing.\n");

	int IsCaptureTrue = 0;
	int IsFrameCaptureTrue = 0;
	int FrameCount = 0;
	int IsCamPacketOK[4];

	int p[3]; // last parameter of cvSaveImage() that sets image quality
	p[0] = CV_IMWRITE_JPEG_QUALITY; //p[0] = CV_IMWRITE_PNG_COMPRESSION;
	p[1] = 100; // 0-100 for jpeg images where 100 is best quality. for png images 0 means no compression. max value is 9.
	p[2] = 0;

	CvFont font; // This is setting the font size and other properties for the text we superimpose on images.
	cvInitFont(&font, CV_FONT_HERSHEY_TRIPLEX, 0.8, 0.8, 0, 1, 16);
	//char avg_fps_display[15];

	//==========================================================================================================================================================
	//==================================================== start image acquisition ==============================================================================
	//==========================================================================================================================================================
	for (;;)
	{
		time_start = GetTickCount();
		static DWORD initialTime = time_start;
		// Software trigger
		for (i = 0; i<numCameras; i++)
			if (PvCommandRun(Camera[i].Handle, "FrameStartTriggerSoftware") != ePvErrSuccess)
				printf("\nFrameStartTriggerSoftware: Cam%d", i);

		// Wait for camera to return to host
		for (i = 0; i<numCameras; i++)
			if (PvCaptureWaitForFrameDone(Camera[i].Handle, &(Camera[i].Frame), PVINFINITE) != ePvErrSuccess)
				printf("\nPvCaptureWaitForFrameDone: Cam%d", i);

		// check returned Frame.Status
		IsPacketOK = 1;
		for (i = 0; i<numCameras; i++)
		{
			if (Camera[i].Frame.Status != ePvErrSuccess)
				IsCamPacketOK[i] = 0;
			else
				IsCamPacketOK[i] = 1;

			IsPacketOK *= IsCamPacketOK[i];
		}

		// Print out Packet status
		printf("\nFrame-%06d, Packet chk: ", FrameCount);
		for (i = 0; i<numCameras; i++)
			printf("%s", IsCamPacketOK[i] ? "o" : "x");
		printf("=%s,", IsPacketOK ? "GOOD" : "FAIL");

		printf(" FPS:%5.2lf", fps); // Show FPS of previous frame
		printf(" AvgFPS:%5.2lf", avg_fps); // Show average FPS

		// Copy images to memory
		Concurrency::parallel_for((unsigned long)0, numCameras, [&](unsigned long i)
		{
			memcpy(img_ori[i]->imageDataOrigin, (char*)Camera[i].Frame.ImageBuffer, Camera[i].Frame.ImageBufferSize);
			//img_original[i] = cvCloneImage(img_ori[i]); /* we clone the image for saving purposes */
			if (displayImagesOnScreen)
				cvResize(img_ori[i], img_sml[i], INTER_LINEAR);
		});

		// For drawing
		if (displayImagesOnScreen)
		{
			for (i = 0; i < numCameras; i++)
			{
				// Copying img_shw to the img_shw_big to show result
				cvSetImageROI(img_shw_big, rtShwBig[i]);
				cvCopy(img_sml[i], img_shw_big);
				cvResetImageROI(img_shw_big);
			}


			// Displaying image stream. Go and uncomment cvNamedWindow(WindowName, 1) too.
			cvCvtColor(img_shw_big, img_shw_big_color, CV_GRAY2BGR); /* convert from 1 channel to 3 channels to superimpose colorful text on images */
			cvPutText(img_shw_big_color, "Left Cam", cvPoint((img_shw_big->width) / 2 - 130, 25), &font, cvScalar(0, 0, 255));
			cvPutText(img_shw_big_color, "Right Cam", cvPoint((img_shw_big->width) - 150, 25), &font, cvScalar(0, 0, 255));
			//sprintf(avg_fps_display, "Avg FPS = %.2f", avg_fps);
			//cvPutText(img_shw_big, avg_fps_display, cvPoint(25, 25), &font, cvScalar(128));
			//putText(Mat(img_shw_big, false), "Camera 1", Point(964 / showingratio - 175, 30), CV_FONT_HERSHEY_SIMPLEX, 1, Scalar(0, 0, 255), 2.4, CV_AA);
			//putText(Mat(img_shw_big, false), "Frame #" + to_string(FrameCount), Point(15, 30), CV_FONT_HERSHEY_TRIPLEX, 1, Scalar(255, 0, 0), 1.4, CV_AA);
			//putText(Mat(img_shw_big, false), "AVG FPS = " + to_string(avg_fps), Point(15, 50), CV_FONT_HERSHEY_TRIPLEX, 1, Scalar(0, 255, 0), 1.4, CV_AA);
			imshow(WindowName, Mat(img_shw_big_color, false)); // to skip viewing image stream, comment this line.
			//cvShowImage(WindowName, img_shw_big); // to skip viewing image stream, comment this line.
			moveWindow(WindowName, 680, 10);

			// Saving
			if (IsCaptureTrue || IsFrameCaptureTrue)
			{
				for (i = 0; i < numCameras; i++)
				{
					//sprintf_s((char*)(SavedImageFileName[i]), Sizeof_SavedImageFileName, "CAM%d_image_%04d.jpg", i, nCount); // you may save the image as jpg too.
					sprintf_s((char*)(SavedImageFileName[i]), Sizeof_SavedImageFileName, "output_images/CAM%d_im%d.jpg", i, nCount); // you may save the image as jpg too.
					//sprintf_s((char*)(SavedImageFileNameOriginal[i]), Sizeof_SavedImageFileNameOriginal, "original_output_images/CAM%d_ori%d.jpg", i, nCount);
				}
				Concurrency::parallel_for((unsigned long)0, numCameras, [&](unsigned long i)
				{
					cvSaveImage(SavedImageFileName[i], img_ori[i], p); // last parameter is for quality of jpg or png image.
					//cvSaveImage(SavedImageFileNameOriginal[i], img_original[i], p); // last parameter is for quality of jpg or png image.
				});
				printf(", Images were saved %04d", nCount);
				IsFrameCaptureTrue = 0;
				nCount++;
			}

			// Key control
			c = cvWaitKey(1);
			if (c != -1)
			{
				if ((char)c == VK_ESCAPE) // ESC character
				{
					printf("\n");
					break;
				}
				if ((char)c == VK_CAPTURE_IMAGE) // press 'f' to capture image (you must be on imshow() region, not console nor some other display)
				{
					IsFrameCaptureTrue = 1;
				}
				if ((char)c == 32)   // 32 is ASCII code for spacebar
				{
					if (IsCaptureTrue)
					{
						IsCaptureTrue = 0;
						printf("\n\n*********Capture stopped!***************\n");
						time_end_saving = GetTickCount();
						elapsedtime_saving = time_end_saving - time_start_saving;
						printf("\n**Summary**\nElaptime:% .1lf\nTotal frame saved: %d\nFPS: %.1lf\n", elapsedtime_saving, nCount, (double)nCount / elapsedtime_saving*1000.0);
						break;
					}
					else
					{
						IsCaptureTrue = 1;
						printf("\n\n*********Capture started!***************\n");
						time_start_saving = GetTickCount();
					}
				}
			}
		}

		FrameCount++;

		// requeue frame
		for (i = 0; i<numCameras; i++)
		{
			if (PvCaptureQueueFrame(Camera[i].Handle, &(Camera[i].Frame), NULL) != ePvErrSuccess)
			{
				printf("PvCaptureQueueFrame: Cam%d", i);
			}
		}

		// Showing additional info
		if (!IsPacketOK)
			printf(" **PACKET LOSS**");

		/* Calculating FPS */
		time_end = GetTickCount();
		fps = 1000.0 / (double)(time_end - time_start); // 1s = 1000ms
		sum_fps += fps;
		avg_fps = sum_fps / FrameCount;
	}
	//==============================================================================================================================================================
	//====================================================== end of image acquisition ==============================================================================
	//==============================================================================================================================================================

	for (i = 0; i<numCameras; i++)
	{
		PvCommandRun(Camera[i].Handle, "AcquisitionStop");
		PvCaptureQueueClear(Camera[i].Handle);
		PvCaptureEnd(Camera[i].Handle);
		PvCameraClose(Camera[i].Handle);
		delete[](char*)Camera[i].Frame.ImageBuffer;
	}

	PvUnInitialize();
	if (displayImagesOnScreen)
		cvDestroyAllWindows();

	for (i = 0; i<numCameras; i++)
		delete[] SavedImageFileName[i];
	delete[] SavedImageFileName;

	for (i = 0; i<numCameras; i++)
	{
		cvReleaseImage(&img_ori[i]); // cvReleaseImage(&img) are used to avoid memory leaks
		cvReleaseImage(&img_sml[i]);
		//cvReleaseImage(&img_original[i]);
	}

	delete[] img_ori;
	delete[] img_sml;
	//delete[] img_original;
	delete[] Camera;
	delete[] list;
	
	cout << "Compiled with OpenCV version " << CV_VERSION << endl; // thanks to Shervin Emami
	system("pause");
}