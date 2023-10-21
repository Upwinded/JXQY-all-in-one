#include "IMP.h"


bool IMP::cmpIMGHead(_shared_imp img)
{
	if (img == nullptr)
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

unsigned int IMP::getIMPImageActionTime(_shared_imp impImage)
{
	if (impImage == nullptr)
	{
		return 0;
	}
	if (impImage->directions < 1)
	{
		impImage->directions = 1;
	}

	if (impImage == nullptr || impImage->frame.size() == 0 )
	{
		return 0;
	}

	int framePerDirection = impImage->frame.size() / impImage->directions;
	return impImage->interval * framePerDirection;
}

bool IMP::loadIMPImage(_shared_imp impImage, const std::string & fileName, bool directlyLoad)
{
	std::unique_ptr<char[]> data;
	int size = PakFile::readFile(fileName, data);
	if (size > 0)
	{
		if (loadIMPImageFromMem(impImage, data, size, directlyLoad))
		{
			return true;
		}
	}
	return false;
}

bool IMP::loadIMPImageFromMem(_shared_imp impImage, std::unique_ptr<char[]>& data, int size, bool directlyLoad)
{
	int imageHeadLen = imgHeadLen + 4 * 3 + 4 * imageNullLen;
	if (impImage == nullptr || data == nullptr || size < imageHeadLen)
	{
		return false;
	}

	clearIMPImage(impImage);

	auto data_ptr = data.get();

	memcpy(impImage->head, data_ptr, imgHeadLen);
	data_ptr += imgHeadLen;

	if (!cmpIMGHead(impImage))
	{
		return false;
	}
	int frameCount = 0;
	memcpy(&frameCount, data_ptr, 4);
	data_ptr += 4;
	memcpy(&impImage->directions, data_ptr, 4);
	data_ptr += 4;
	memcpy(&impImage->interval, data_ptr, 4);
	data_ptr += 4;

	for (int i = 0; i < imageNullLen; i++)
	{
		memcpy(&impImage->imageNull[i], data_ptr, 4);
		data_ptr += 4;
	}

	size -= imageHeadLen;
	
	impImage->frame.resize(frameCount);
	for (size_t i = 0; i < impImage->frame.size(); i++)
	{
		if (size >= 4 * 3 + 4 * frameNullLen)
		{
			memcpy(&impImage->frame[i].dataLen, data_ptr, 4);
			data_ptr += 4;
			memcpy(&impImage->frame[i].xOffset, data_ptr, 4);
			data_ptr += 4;
			memcpy(&impImage->frame[i].yOffset, data_ptr, 4);
			data_ptr += 4;
			for (size_t j = 0; j < frameNullLen; j++)
			{
				memcpy(&impImage->frame[i].frameNull[j], data_ptr, 4);
				data_ptr += 4;
			}
			size -= 4 * 3 + 4 * frameNullLen;
			if (size >= impImage->frame[i].dataLen && impImage->frame[i].dataLen > 0)
			{
				if (impImage->frame[i].image != nullptr)
				{
					//Engine::getInstance()->freeImage(impImage->frame[i].image);
					impImage->frame[i].image = nullptr;
				}
				if (impImage->frame[i].data != nullptr)
				{
					//delete[] impImage->frame[i].data;
					impImage->frame[i].data = nullptr;
				}			
				if (impImage->frame[i].dataLen > 0)
				{
					impImage->frame[i].data = std::make_unique<char[]>(impImage->frame[i].dataLen);
					memcpy(&impImage->frame[i].data[0], data_ptr, impImage->frame[i].dataLen);
					size -= impImage->frame[i].dataLen;
					data_ptr += impImage->frame[i].dataLen;
					if (directlyLoad)
					{
						impImage->frame[i].image = Engine::getInstance()->loadImageFromMem(impImage->frame[i].data, impImage->frame[i].dataLen);
						impImage->frame[i].data = nullptr;
						impImage->frame[i].dataLen = 0;
					}
				}
			}
			else
			{
				impImage->frame[i].data = nullptr;
				impImage->frame[i].image = nullptr;
				impImage->frame[i].dataLen = 0;
			}
		}
		else
		{
			impImage->frame[i].data = nullptr;
			impImage->frame[i].image = nullptr;
			impImage->frame[i].dataLen = 0;
			impImage->frame[i].xOffset = 0;
			impImage->frame[i].yOffset = 0;
		}
	}
	return true;
}

bool IMP::loadIMPImageFromFile(_shared_imp impImage, const std::string& fileName, bool directlyLoad)
{
	if (impImage == nullptr)
	{
		return false;
	}

	if (File::fileExist(fileName))
	{
		std::unique_ptr<char[]> data;
		int size;

		if (File::readFile(fileName, data, size))
		{
			bool result = loadIMPImageFromMem(impImage, data, size, directlyLoad);
			return result;
		}
		return false;
	}
	return false;
}

bool IMP::loadIMPImageFromPak(_shared_imp impImage, const std::string & fileName, const std::string & pakName, bool directlyLoad, bool firstReadPak)
{
	std::unique_ptr<char[]> data;
	int size;
	if (PakFile::readFile(fileName, data, size, pakName, firstReadPak) > 0)
	{
		if (loadIMPImageFromMem(impImage, data, size, directlyLoad))
		{
			return true;
		}
	}	
	return false;
}

void IMP::copyIMPImage(_shared_imp dst, _shared_imp src)
{
	if (dst != nullptr && src != nullptr)
	{
		for (int i = 0; i < imgHeadLen; i++)
		{
			dst->head[i] = src->head[i];
		}
		dst->directions = src->directions;
		dst->interval = src->interval;
		for (int i = 0; i < imageNullLen; i++)
		{
			dst->imageNull[i] = src->imageNull[i];
		}
		dst->frame.resize(dst->frame.size());
		for (size_t i = 0; i < dst->frame.size(); i++)
		{
			dst->frame[i].dataLen = src->frame[i].dataLen;
			dst->frame[i].xOffset = src->frame[i].xOffset;
			dst->frame[i].yOffset = src->frame[i].yOffset;
			if (dst->frame[i].image != nullptr)
			{
				//Engine::getInstance()->freeImage(dst->frame[i].image);
				dst->frame[i].image = nullptr;
			}			
			for (int j = 0; j < frameNullLen; j++)
			{
				dst->frame[i].frameNull[j] = src->frame[i].frameNull[j];
			}
			if (dst->frame[i].data != nullptr)
			{
				//delete[] dst->frame[i].data;
				dst->frame[i].data = nullptr;
			}
			if (src->frame[i].image != nullptr)
			{
				dst->frame[i].image = Engine::getInstance()->createNewImageFromImage(src->frame[i].image);
			}
			else if (src->frame[i].dataLen > 0)
			{
				dst->frame[i].data = std::make_unique<char[]>(dst->frame[i].dataLen);
				memcpy(&(dst->frame[i].data[0]), &(src->frame[i].data[0]), dst->frame[i].dataLen);
			}
		}
	}
}

_shared_imp IMP::createIMPImageFromFile(const std::string& fileName, bool directlyLoad)
{
	if (File::fileExist(fileName))
	{
		std::unique_ptr<char[]> data;
		int size;
		if (File::readFile(fileName, data, size))
		{
			auto result = createIMPImageFromMem(data, size, directlyLoad);
			return result;
		}
	}
	return nullptr;
}

_shared_imp IMP::createIMPImage(const std::string & fileName, bool directlyLoad)
{
	auto impImage = make_shared_imp();
	if (loadIMPImage(impImage, fileName, directlyLoad))
	{
		return impImage;
	}
	else
	{
		return nullptr;
	}
}

_shared_imp IMP::createIMPImage(unsigned int fileID, bool directlyLoad)
{
	std::unique_ptr<char[]> s;
	int len = 0;
	len = PakFile::readFile(fileID, s);
	if (s != nullptr && len > 0)
	{
		return IMP::createIMPImageFromMem(s, len, directlyLoad);
	}
	return nullptr;
}

_shared_imp IMP::createIMPImageFromPak(const std::string & fileName, bool directlyLoad, const std::string & pakName, bool firstReadPak)
{
	auto impImage = make_shared_imp();
	if (loadIMPImageFromPak(impImage, fileName, pakName, directlyLoad, firstReadPak))
	{
		return impImage;
	}
	else
	{
		return nullptr;
	}
}

_shared_imp IMP::createIMPImageFromMem(std::unique_ptr<char[]>& data, int size, bool directlyLoad)
{
	auto impImage = make_shared_imp();
	if (loadIMPImageFromMem(impImage, data, size, directlyLoad))
	{
		return impImage;
	}
	else
	{
		return nullptr;
	}	
}

_shared_imp IMP::createIMPImageFromImage(_shared_image img)
{
	auto impImage = make_shared_imp();
	impImage->directions = 1;
	impImage->frame.resize(0);
	impImage->interval = 0;
	if (img != nullptr)
	{
		impImage->frame.resize(1);
		impImage->frame[0].data = nullptr;
		impImage->frame[0].dataLen = 0;
		impImage->frame[0].image = img;
		impImage->frame[0].xOffset = 0;
		impImage->frame[0].yOffset = 0;
	}
	return impImage;
}

_shared_imp IMP::createIMPImageFromPNG(std::string pngName, bool directlyLoad)
{
	std::unique_ptr<char[]> s;
	auto len = PakFile::readFile(pngName, s);
	if (len <  0 || s == nullptr)
	{
		return nullptr;
	}
	auto impImage = make_shared_imp();
	impImage->directions = 1;
	impImage->interval = 0;
	impImage->frame.resize(1);
	impImage->frame[0].data = std::move(s);
	impImage->frame[0].dataLen = len;
	impImage->frame[0].image = nullptr;
	impImage->frame[0].xOffset = 0;
	impImage->frame[0].yOffset = 0;
	if (directlyLoad)
	{
		impImage->frame[0].image = Engine::getInstance()->loadImageFromMem(impImage->frame[0].data, impImage->frame[0].dataLen);
		impImage->frame[0].dataLen = 0;
		impImage->frame[0].data = nullptr;
	}

	return impImage;
}

_shared_imp IMP::createIMPImageFromFrame(_shared_imp impImage, int index)
{
	if (impImage == nullptr || impImage->frame.size() == 0)
	{
		return nullptr;
	}
	if (index < 0 || index >= (int)impImage->frame.size())
	{
		return nullptr;
	}
	auto img = make_shared_imp();
	img->directions = 1;
	img->interval = impImage->interval;
	img->frame.resize(1);
	img->frame[0].dataLen = impImage->frame[index].dataLen;
	img->frame[0].xOffset = impImage->frame[index].xOffset;
	img->frame[0].yOffset = impImage->frame[index].yOffset;
	if (impImage->frame[index].image != nullptr)
	{
		img->frame[0].image = Engine::getInstance()->createNewImageFromImage(impImage->frame[index].image);
	}
	else
	{
		if (img->frame[0].dataLen > 0)
		{
			img->frame[0].data = std::make_unique<char[]>(img->frame[0].dataLen);
			memcpy(&img->frame[0].data[0], &impImage->frame[index].data[0], img->frame[0].dataLen);
		}
		else
		{
			img->frame[0].data = nullptr;
		}
	}
	return img;
}

//Çå³ýIMPImageÍ¼Æ¬ËùÓÐÖ¡
void IMP::clearIMPImage(_shared_imp impImage)
{
	if (impImage == nullptr)
	{
		return;
	}
	for (int j = 0; j < (int)impImage->frame.size(); j++)
	{
		impImage->frame[j].dataLen = 0;
		if (impImage->frame[j].image != nullptr)
		{
			//Engine::getInstance()->freeImage((_shared_image)impImage->frame[j].image);
			impImage->frame[j].image = make_shared_image(nullptr);
		}
		if (impImage->frame[j].data != nullptr)
		{
			//delete[] impImage->frame[j].data;
			impImage->frame[j].data = nullptr;
		}
		impImage->frame[j].xOffset = 0;
		impImage->frame[j].yOffset = 0;

		for (int k = 0; k < frameNullLen; k++)
		{
			impImage->frame[j].frameNull[k] = 0;
		}
	}
	impImage->frame.resize(0);
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

_shared_image IMP::loadImage(_shared_imp impImage, int index, int * xOffset, int * yOffset)
{
	if (impImage == nullptr)
	{
		return nullptr;
	}
	if (index >= 0 && index < impImage->frame.size())
	{
		if (xOffset != nullptr)
		{
			*xOffset = impImage->frame[index].xOffset;
		}
		if (yOffset != nullptr)
		{
			*yOffset = impImage->frame[index].yOffset;
		}
		if (impImage->frame[index].image != nullptr)
		{
			return impImage->frame[index].image;
		}
		else if (impImage->frame[index].data != nullptr && impImage->frame[index].dataLen > 0)
		{
			impImage->frame[index].image = Engine::getInstance()->loadImageFromMem(impImage->frame[index].data, impImage->frame[index].dataLen);
			impImage->frame[index].dataLen = 0;
			//delete[] impImage->frame[index].data;
			impImage->frame[index].data = nullptr;
			return impImage->frame[index].image;
		}
	}
	return nullptr;
}

_shared_image IMP::loadImageForTime(_shared_imp impImage, UTime time, int * xOffset, int * yOffset, bool once, bool reverse)
{
	if (impImage == nullptr)
	{
		return nullptr;
	}
	int directions = impImage->directions;
	impImage->directions = 1;
	_shared_image image = loadImageForDirection(impImage, 0, time, xOffset, yOffset, once, reverse);
	impImage->directions = directions;
	return image;
}

_shared_image IMP::loadImageForDirection(_shared_imp impImage, int direction, UTime time, int * xOffset, int * yOffset, bool once, bool reverse)
{
	if (impImage == nullptr)
	{
		return nullptr;
	}
	if (impImage->directions < 1)
	{
		impImage->directions = 1;
	}
	if (impImage == nullptr || impImage->frame.size() == 0)
	{
		return nullptr;
	}
	if (direction < 0)
	{
		direction = 0;
	}
	if (direction >= impImage->directions)
	{
		direction = direction % impImage->directions;
	}
	if (direction >= impImage->frame.size())
	{
		direction = 0;
	}

	int framePerDirection = impImage->frame.size() / impImage->directions;
	if (framePerDirection <= 0)
	{
		framePerDirection = 1;
	}
    if (impImage->interval < 0)
    {
        impImage->interval = 0;
    }
    int index = 0;

    bool end = (impImage->interval > 0) ? time / impImage->interval >= framePerDirection : false;
    if (end && once) {
        index = reverse ? 0 : framePerDirection - 1;
    }
    else
    {
        index = (impImage->interval > 0) ? ((time / impImage->interval) % framePerDirection) : (0);
        if (reverse)
        {
            index = framePerDirection - 1 - index;
        }
    }

	index += framePerDirection * direction;
	if (index < impImage->frame.size() && index >= 0)
	{
		return loadImage(impImage, index, xOffset, yOffset);
	}
	return nullptr;
}

_shared_image IMP::loadImageForLastFrame(_shared_imp impImage, int direction, int * xOffset, int * yOffset, bool reverse)
{
	if (impImage == nullptr)
	{
		return nullptr;
	}
	if (impImage->directions < 1)
	{
		impImage->directions = 1;
	}

	if (impImage == nullptr || impImage->frame.size() == 0)
	{
		return nullptr;
	}
	if (direction < 0)
	{
		direction = 0;
	}
	if (direction >= impImage->directions)
	{
		direction = direction % impImage->directions;
	}
	if (direction >= impImage->frame.size())
	{
		direction = 0;
	}
	int fpd = impImage->frame.size() / impImage->directions;
    int index = 0;
    if (reverse)
    {
        index = fpd * direction;
    }
    else
    {
        index = fpd * (direction + 1) - 1;

    }
	if (index < impImage->frame.size() && index >= 0)
	{
		return loadImage(impImage, index, xOffset, yOffset);
	}
	return nullptr;
}
