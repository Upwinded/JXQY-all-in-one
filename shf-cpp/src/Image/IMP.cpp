#include "IMP.h"

IMP IMP::imp;
IMP *IMP::this_ = &IMP::imp;

IMP::IMP()
{	
}

IMP::~IMP()
{
}

IMP * IMP::getInstance()
{
	return this_;
}

bool IMP::cmpIMGHead(IMPImage * img)
{
	if (img == NULL)
	{
		return false;
	}

	char tempIMGHead[imgHeadLen] = imgHeadString;

	for (int i = 0; i < imgHeadLen; i++)
	{
		if (tempIMGHead[i] != img->head[i])
		{
			return false;
		}
	}

	return true;
}

unsigned int IMP::getIMPImageActionTime(IMPImage * impImage)
{
	if (impImage == NULL)
	{
		return NULL;
	}
	if (impImage->directions < 1)
	{
		impImage->directions = 1;
	}

	if (impImage == NULL || impImage->frameCount <= 0 || impImage->frameCount != (int)impImage->frame.size())
	{
		return 0;
	}

	int fpd = impImage->frameCount / impImage->directions;
	return impImage->interval * fpd;
}

bool IMP::loadIMPImage(IMPImage * impImage, const std::string & fileName)
{
	char* data;
	int size = PakFile::readFile(fileName, &data);
	if (size > 0)
	{
		if (loadIMPImageFromMem(impImage, data, size))
		{
			delete[] data;
			return true;
		}
		delete[] data;
	}
	return false;
}

bool IMP::loadIMPImageFromMem(IMPImage * impImage, char *data, int size)
{
	int imageHeadLen = imgHeadLen + 4 * 3 + 4 * imageNullLen;
	if (impImage == NULL || data == NULL || size < imageHeadLen)
	{
		return false;
	}

	clearIMPImage(impImage);

	memcpy(impImage->head, data, imgHeadLen);
	data += imgHeadLen;

	if (!cmpIMGHead(impImage))
	{
		return false;
	}

	memcpy(&impImage->frameCount, data, 4);
	data += 4;
	memcpy(&impImage->directions, data, 4);
	data += 4;
	memcpy(&impImage->interval, data, 4);
	data += 4;

	for (int i = 0; i < imageNullLen; i++)
	{
		memcpy(&impImage->imageNull[i], data, 4);
		data += 4;
	}
	if (impImage->frameCount < 0)
	{
		impImage->frameCount = 0;
	}

	size -= imageHeadLen;
	
	impImage->frame.resize(impImage->frameCount);
	for (int i = 0; i < impImage->frameCount; i++)
	{
		if (size >= 4 * 3 + 4 * frameNullLen)
		{
			memcpy(&impImage->frame[i].dataLen, data, 4);
			data += 4;
			memcpy(&impImage->frame[i].xOffset, data, 4);
			data += 4;
			memcpy(&impImage->frame[i].yOffset, data, 4);
			data += 4;
			for (int j = 0; j < frameNullLen; j++)
			{
				memcpy(&impImage->frame[i].frameNull[j], data, 4);
				data += 4;
			}
			size -= 4 * 3 + 4 * frameNullLen;
			if (size >= impImage->frame[i].dataLen && impImage->frame[i].dataLen > 0)
			{
				if (impImage->frame[i].image != NULL)
				{
					Engine::getInstance()->freeImage(impImage->frame[i].image);
				}
				if (impImage->frame[i].data != NULL)
				{
					delete[] impImage->frame[i].data;
					impImage->frame[i].data = NULL;
				}			
				if (impImage->frame[i].dataLen > 0)
				{
					impImage->frame[i].data = new char[impImage->frame[i].dataLen];
					memcpy(&impImage->frame[i].data[0], data, impImage->frame[i].dataLen);
					size -= impImage->frame[i].dataLen;
					data += impImage->frame[i].dataLen;
				}
			}
			else
			{
				impImage->frame[i].data = NULL;
				impImage->frame[i].image = NULL;
				impImage->frame[i].dataLen = 0;
			}
		}
		else
		{
			impImage->frame[i].data = NULL;
			impImage->frame[i].image = NULL;
			impImage->frame[i].dataLen = 0;
			impImage->frame[i].xOffset = 0;
			impImage->frame[i].yOffset = 0;
		}
	}
	return true;
}

bool IMP::loadIMPImageFromFile(IMPImage * impImage, const std::string& fileName)
{
	if (impImage == NULL)
	{
		return false;
	}

	if (File::fileExist(fileName))
	{
		char* data;
		int size;

		if (File::readFile(fileName, &data, &size))
		{
			bool result = loadIMPImageFromMem(impImage, data, size);
			delete[] data;
			return result;
		}

		return false;
	}

	return false;
}

bool IMP::loadIMPImageFromPak(IMPImage * impImage, const std::string & fileName, const std::string & pakName, bool firstReadPak)
{
	char* data;
	int size;
	if (PakFile::readFile(fileName, &data, &size, pakName, firstReadPak) > 0)
	{
		if (loadIMPImageFromMem(impImage, data, size))
		{
			delete[] data;
			return true;
		}
		delete[] data;
	}	
	return false;
}

void IMP::copyIMPImage(IMPImage * dst, IMPImage * src)
{
	if (dst != NULL && src != NULL)
	{
		for (int i = 0; i < imgHeadLen; i++)
		{
			dst->head[i] = src->head[i];
		}
		dst->frameCount = src->frameCount;
		dst->directions = src->directions;
		dst->interval = src->interval;
		for (int i = 0; i < imageNullLen; i++)
		{
			dst->imageNull[i] = src->imageNull[i];
		}
		dst->frame.resize(dst->frameCount);
		for (int i = 0; i < dst->frameCount; i++)
		{
			dst->frame[i].dataLen = src->frame[i].dataLen;
			dst->frame[i].xOffset = src->frame[i].xOffset;
			dst->frame[i].yOffset = src->frame[i].yOffset;
			if (dst->frame[i].image != NULL)
			{
				Engine::getInstance()->freeImage(dst->frame[i].image);
				dst->frame[i].image = NULL;
			}			
			for (int j = 0; j < frameNullLen; j++)
			{
				dst->frame[i].frameNull[j] = src->frame[i].frameNull[j];
			}
			if (dst->frame[i].data != NULL)
			{
				delete[] dst->frame[i].data;
				dst->frame[i].data = NULL;
			}
			if (src->frame[i].image != NULL)
			{
				dst->frame[i].image = Engine::getInstance()->createNewImageFromImage(src->frame[i].image);
			}
			else if (src->frame[i].dataLen > 0)
			{
				dst->frame[i].data = new char[dst->frame[i].dataLen];
				memcpy(&(dst->frame[i].data[0]), &(src->frame[i].data[0]), dst->frame[i].dataLen);
			}
		}
	}
}

IMPImage * IMP::createIMPImageFromFile(const std::string& fileName)
{
	if (File::fileExist(fileName))
	{
		char* data;
		int size;
		if (File::readFile(fileName, &data, &size))
		{
			IMPImage* result = createIMPImageFromMem(data, size);
			delete[] data;
			return result;
		}
	}
	return nullptr;
}

IMPImage * IMP::createIMPImage(const std::string & fileName)
{
	IMPImage * impImage = new IMPImage;
	if (loadIMPImage(impImage, fileName))
	{
		return impImage;
	}
	else
	{
		clearIMPImage(impImage);
		delete impImage;
		return nullptr;
	}
}

IMPImage * IMP::createIMPImage(unsigned int fileID)
{
	char * s = NULL;
	int len = 0;
	len = PakFile::readFile(fileID, &s);
	if (s != NULL && len > 0)
	{
		IMPImage * impImage = IMP::createIMPImageFromMem(s, len);
		delete[] s;
		return impImage;
	}
	return nullptr;
}

IMPImage * IMP::createIMPImageFromPak(const std::string & fileName, const std::string & pakName, bool firstReadPak)
{
	IMPImage * impImage = new IMPImage;
	if (loadIMPImageFromPak(impImage, fileName, pakName, firstReadPak))
	{
		return impImage;
	}
	else
	{
		clearIMPImage(impImage);
		delete impImage;
		return nullptr;
	}
}

IMPImage * IMP::createIMPImageFromMem(char * data, int size)
{
	IMPImage * impImage = new IMPImage;
	if (loadIMPImageFromMem(impImage, data, size))
	{
		return impImage;
	}
	else
	{
		clearIMPImage(impImage);
		delete impImage;
		return nullptr;
	}	
}

IMPImage * IMP::createIMPImageFromImage(_image img)
{
	IMPImage * impImage = new IMPImage;
	impImage->directions = 1;
	impImage->frameCount = 0;
	impImage->frame.resize(0);
	impImage->interval = 0;
	if (img != NULL)
	{
		impImage->frameCount = 1;
		impImage->frame.resize(1);
		impImage->frame[0].data = NULL;
		impImage->frame[0].dataLen = 0;
		impImage->frame[0].image = img;
	}
	return impImage;
}

IMPImage * IMP::createIMPImageFromFrame(IMPImage * impImage, int index)
{
	if (impImage == NULL || impImage->frameCount <= 0)
	{
		return nullptr;
	}
	if (index < 0 || index >= impImage->frameCount)
	{
		return nullptr;
	}
	IMPImage * img = new IMPImage;
	img->directions = 1;
	img->frameCount = 1;
	img->interval = impImage->interval;
	img->frame.resize(1);
	img->frame[0].dataLen = impImage->frame[index].dataLen;
	img->frame[0].xOffset = impImage->frame[index].xOffset;
	img->frame[0].yOffset = impImage->frame[index].yOffset;
	if (impImage->frame[index].image != NULL)
	{
		img->frame[0].image = Engine::getInstance()->createNewImageFromImage(impImage->frame[index].image);
	}
	else
	{
		if (img->frame[0].dataLen > 0)
		{
			img->frame[0].data = new char[img->frame[0].dataLen];
			memcpy(&img->frame[0].data[0], &impImage->frame[index].data[0], img->frame[0].dataLen);
		}
		else
		{
			img->frame[0].data = NULL;
		}
	}
	return img;
}

//Çå³ýIMPImageÍ¼Æ¬ËùÓÐÖ¡
void IMP::clearIMPImage(IMPImage * impImage)
{
	if (impImage == NULL)
	{
		return;
	}
	for (int j = 0; j < (int)impImage->frame.size(); j++)
	{
		impImage->frame[j].dataLen = 0;
		if (impImage->frame[j].image != NULL)
		{
			Engine::getInstance()->freeImage((_image)impImage->frame[j].image);
		}
		if (impImage->frame[j].data != NULL)
		{
			delete[] impImage->frame[j].data;
			impImage->frame[j].data = NULL;
		}
		impImage->frame[j].xOffset = 0;
		impImage->frame[j].yOffset = 0;

		for (int k = 0; k < frameNullLen; k++)
		{
			impImage->frame[j].frameNull[k] = 0;
		}
	}
	impImage->frameCount = 0;
	impImage->frame.resize(impImage->frameCount);
	impImage->directions = 0;
	impImage->interval = 0;

	for (int j = 0; j < imgHeadLen; j++)
	{
		impImage->head[j] = 0;
	}

	for (int j = 0; j < imageNullLen; j++)
	{
		impImage->imageNull[j] = 0;
	}
}

_image IMP::loadImage(IMPImage * impImage, int index, int * xOffset, int * yOffset)
{
	if (impImage == NULL)
	{
		return NULL;
	}
	if (index >= 0 && index < impImage->frameCount && impImage->frameCount == (int)impImage->frame.size())
	{
		if (xOffset != NULL)
		{
			*xOffset = impImage->frame[index].xOffset;
		}
		if (yOffset != NULL)
		{
			*yOffset = impImage->frame[index].yOffset;
		}
		if (impImage->frame[index].image != NULL)
		{
			return (_image)impImage->frame[index].image;
		}
		else if (impImage->frame[index].data != NULL && impImage->frame[index].dataLen > 0)
		{
			impImage->frame[index].image = (void *)Engine::getInstance()->loadImageFromMem(impImage->frame[index].data, impImage->frame[index].dataLen);
			impImage->frame[index].dataLen = 0;
			delete[] impImage->frame[index].data;
			impImage->frame[index].data = NULL;
			return (_image)impImage->frame[index].image;
		}
	}
	return NULL;
}

_image IMP::loadImageForTime(IMPImage * impImage, unsigned int time, int * xOffset, int * yOffset)
{
	if (impImage == NULL)
	{
		return NULL;
	}
	int directions = impImage->directions;
	impImage->directions = 1;
	_image image = loadImageForDirection(impImage, 0, time, xOffset, yOffset);
	impImage->directions = directions;
	return image;

}

_image IMP::loadImageForDirection(IMPImage * impImage, int direction, unsigned int time, int * xOffset, int * yOffset)
{
	if (impImage == NULL)
	{
		return NULL;
	}
	if (impImage->directions < 1)
	{
		impImage->directions = 1;
	}
	if (impImage == NULL || impImage->frameCount <= 0 || impImage->frameCount != (int)impImage->frame.size())
	{
		return NULL;
	}
	if (direction < 0)
	{
		direction = 0;
	}
	if (direction >= impImage->directions)
	{
		direction = direction % impImage->directions;
	}
	if (direction >= impImage->frameCount)
	{
		direction = 0;
	}

	int fpd = impImage->frameCount / impImage->directions;
	if (fpd <= 0)
	{
		fpd = 1;
	}
	int index = (impImage->interval > 0) ? ((time / impImage->interval) % fpd) : (0);
	index += fpd * direction;
	if (index < impImage->frameCount && index >= 0)
	{
		return loadImage(impImage, index, xOffset, yOffset);
	}
	return NULL;
}

_image IMP::loadImageForLastFrame(IMPImage * impImage, int direction, int * xOffset, int * yOffset)
{
	if (impImage == NULL)
	{
		return NULL;
	}
	if (impImage->directions < 1)
	{
		impImage->directions = 1;
	}

	if (impImage == NULL || impImage->frameCount <= 0 || impImage->frameCount != (int)impImage->frame.size())
	{
		return NULL;
	}
	if (direction < 0)
	{
		direction = 0;
	}
	if (direction >= impImage->directions)
	{
		direction = direction % impImage->directions;
	}
	if (direction >= impImage->frameCount)
	{
		direction = 0;
	}
	int fpd = impImage->frameCount / impImage->directions;
	int index = fpd * (direction + 1) - 1;
	if (index < impImage->frameCount && index >= 0)
	{
		return loadImage(impImage, index, xOffset, yOffset);
	}
	return NULL;
}
