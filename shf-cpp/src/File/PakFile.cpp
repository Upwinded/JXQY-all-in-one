#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif 

#include "PakFile.h"

std::vector<std::string> PakFile::pakList = {};
Engine* PakFile::engine = Engine::getInstance();
PakFile PakFile::pakFile;
PakFile * PakFile::this_ = &PakFile::pakFile;

PakFile::PakFile()
{
	engine = Engine::getInstance();
	pakList.resize(1);
	pakList[0] = "data/data.dat";
	/*
	pakList.resize(13);
	pakList[0] = "data/character.dat";
	pakList[1] = "data/effect.dat";
	pakList[2] = "data/font.dat";
	pakList[3] = "data/goods.dat";
	pakList[4] = "data/ini.dat";
	pakList[5] = "data/interface.dat";
	pakList[6] = "data/magic.dat";
	pakList[7] = "data/map.dat";
	pakList[8] = "data/object.dat";
	pakList[9] = "data/portrait.dat";
	pakList[10] = "data/script.dat";
	pakList[11] = "data/sound.dat";
	pakList[12] = "data/tiled.dat";
	*/
}

PakFile::~PakFile()
{
	pakList.resize(0);
}

bool PakFile::cmpPakHead(PakHead * pakHead)
{
	if (pakHead == NULL)
	{
		return false;
	}

	char tempPakHead[pakHeadLen] = pakHeadString;

	for (int i = 0; i < pakHeadLen; i++)
	{
		if (tempPakHead[i] != pakHead->head[i])
		{
			return false;
		}
	}

	return true;
}

unsigned int PakFile::hashFileName(const std::string & fileName)
{
	unsigned int result = 0;
	if (fileName.length() > 0)
	{
		char * ch = new char[fileName.length() + 1];	
		memcpy(ch, fileName.c_str(), fileName.length());
		ch[fileName.length()] = 0;
		unsigned int cnt = 0;
		int i = 0;
		while (ch[i] != 0)
		{
			if (ch[i] == '/')
			{
				ch[i] = '\\';
			}
			if ((ch[i] >= 'A') && (ch[i] <= 'Z'))
			{
				ch[i] = 0x20 + ch[i];
			}
			unsigned int u = ch[i];
			result = (u * (cnt + 1) + result) % 0x8000000B;
			result = (((result ^ 0xFFFFFFFF) + 1) << 4) - result;
			cnt++;
			i++;
		}
		result ^= 0x12345678;
		delete[] ch;
	}
	return result;
}

int PakFile::findFileInPak(unsigned int fileID, const std::string & pakName)
{	
	FILE* fp = fopen(pakName.c_str(), "rb");
	if (fp)
	{
		fseek(fp, 0, 2);
		int size = ftell(fp);
		fseek(fp, 0, 0);
		if (size >= sizeof(PakHead))
		{
			PakHead pakHead;
			fread(&pakHead, sizeof(PakHead), 1, fp);
			
			if (cmpPakHead(&pakHead))
			{				

				if (pakHead.fileCount > 0 && size >= (int)sizeof(PakHead) + pakHead.fileCount * (int)sizeof(PakFileInfo))
				{
					
					PakFileInfo pakFileInfo;
					/*
					for (int i = 0; i < pakHead.fileCount; i++)
					{
						fread(&pakFileInfo, sizeof(PakFileInfo), 1, fp);
						if (fileID == pakFileInfo.fileID)
						{
							fclose(fp);
							return i;
						}
					}
					*/

					int min = 0;
					int max = pakHead.fileCount - 1;

					while (true)
					{
						if (min == max)
						{
							fseek(fp, (int)sizeof(PakHead) + min * (int)sizeof(PakFileInfo), 0);
							fread(&pakFileInfo, sizeof(PakFileInfo), 1, fp);
							if (fileID == pakFileInfo.fileID)
							{
								fclose(fp);
								return min;
							}
							break;
						}
						else if (max > min)
						{
							int temp = (max - min) / 2 + min;						
							fseek(fp, (int)sizeof(PakHead) + temp * (int)sizeof(PakFileInfo), 0);
							fread(&pakFileInfo, sizeof(PakFileInfo), 1, fp);
							if (fileID == pakFileInfo.fileID)
							{
								fclose(fp);
								return temp;
							}
							else if (fileID > pakFileInfo.fileID)
							{
								min = temp + 1;
							}
							else
							{
								max = temp - 1;
							}

						}
						else
						{
							break;
						}
					}
					
				}
			}			
		}
		fclose(fp);
	}
	return -1;
}

int PakFile::findFileInPak(const std::string & fileName, const std::string & pakName)
{
	unsigned int fileID = hashFileName(fileName);
	return findFileInPak(fileID, pakName);
}

FilePos PakFile::findFile(const std::string & fileName, int * index, const std::string & pakName, bool firstReadPak)
{
	if (firstReadPak)
	{
		int i = findFileInPak(fileName, pakName);
		if (i >= 0)
		{
			if (index != NULL)
			{
				*index = i;
			}
			return fileInPak;
		}
		if (File::fileExist(fileName))
		{
			return fileExists;
		}
		return noThisFile;
	}
	else
	{
		if (File::fileExist(fileName))
		{
			return fileExists;
		}
		int i = findFileInPak(fileName, pakName);
		if (i >= 0)
		{
			if (index != NULL)
			{
				*index = i;
			}
			return fileInPak;
		}
		return noThisFile;
	}
}

int PakFile::unpak(char * inBuffer, int inLen, char * outBuffer, int outLen, int compressType)
{
	if (inBuffer == NULL || outBuffer == NULL || inLen == 0 || outLen == 0)
	{
		return 0;
	}
	int size = inLen;
	const int bufferLen = blockSize + blockSize / 8 + 64;
	char tempBuffer[bufferLen];

	int blockCount = outLen / blockSize;
	if ((outLen % blockSize) > 0)
	{
		blockCount++;
	}
	char * p = inBuffer;
	char * outp = outBuffer;
	int outSize = outLen;

	if (blockCount <= 0 || blockCount * 2 > size)
	{
		return 0;
	}

	std::vector<unsigned short> bs;
	bs.resize(blockCount);
	for (size_t i = 0; i < bs.size(); i++)
	{
		memcpy(&bs[i], p, 2);
		p += 2;
		size -= 2;
	}	
	for (int i = 0; i < blockCount; i++)
	{
		unsigned int bSize = bs[i];		
		if (compressType == 2)
		{			
			if (bSize == 0)
			{
				if (i != blockCount - 1)
				{ 
					bSize = blockSize;
				}
				else
				{
					bSize = size;
				}
				if (size < (int)bSize || outSize < (int)bSize)
				{
					bs.resize(0);
					return 0;
				}
				memcpy(outp, p, bSize);
				outp += bSize;
				outSize -= bSize;
			}
			else
			{
				unsigned int out;
				engine->lzoDecompress(p, bSize, &tempBuffer[0], &out);
				if (out > blockSize || outSize < (int)out)
				{
					bs.resize(0);
					return 0;
				}
				memcpy(outp, &tempBuffer[0], out);
				outp += out;
				outSize -= out;
			}
		}
		else
		{
			if (bSize == 0)
			{
				bSize = blockSize;
			}
			if (size < (int)bSize || outSize < (int)bSize)
			{
				bs.resize(0);
				return 0;
			}
			memcpy(outp, p, bSize);
			outp += bSize;
			outSize -= bSize;
		}
		p += bSize;
		size -= bSize;
	}
	bs.resize(0);
	return outLen - outSize;
}

bool PakFile::readPak(const std::string & pakName, int index, char ** s, int * len)
{
	if (index >= 0)
	{
		FILE* fp = fopen(pakName.c_str(), "rb");
		if (fp)
		{
			fseek(fp, 0, 2);
			int size = ftell(fp);
			fseek(fp, 0, 0);
			PakHead pakHead;
			fread(&pakHead, sizeof(PakHead), 1, fp);
			fseek(fp, index * sizeof(PakFileInfo) + sizeof(PakHead), 0);
			PakFileInfo pakFileInfo;
			fread(&pakFileInfo, sizeof(PakFileInfo), 1, fp);
			int fsize = pakFileInfo.fileSize;
			int offset = pakFileInfo.offset;
			int pakSize = size;
			if (index == pakHead.fileCount - 1)
			{
				pakSize -= pakFileInfo.offset;
			}
			else
			{
				pakSize = pakFileInfo.offset;
				fread(&pakFileInfo, sizeof(PakFileInfo), 1, fp);
				pakSize = pakFileInfo.offset - pakSize;
			}

			if (pakSize > 0 && size >= offset + pakSize && fsize > 0)
			{
				char * inBuffer = new char[pakSize];
				fseek(fp, offset, 0);
				fread(inBuffer, 1, pakSize, fp);
				*s = new char[fsize + 1];
				memset(*s, 0, fsize + 1);
				*len = unpak(inBuffer, pakSize, *s, fsize, pakHead.compressType);
				if (*len > 0)
				{
					delete[] inBuffer;
					fclose(fp);
					return true;
				}
				delete[] * s;
				delete[] inBuffer;
			}
			fclose(fp);
		}
	}
	*s = NULL;
	*len = 0;
	return false;
}

bool PakFile::readPak(const std::string & fileName, const std::string & pakName, char ** s, int * len)
{
	int index = findFileInPak(fileName, pakName);
	return readPak(pakName, index, s, len);
}

int PakFile::readFile(unsigned int fileID, char ** s)
{
	if (s == NULL)
	{
		return 0;
	}
	for (size_t i = 0; i < pakList.size(); i++)
	{
		int index = findFileInPak(fileID, pakList[i]);
		if (index >= 0)
		{
			int len;
			if (readPak(pakList[i], index, s, &len))
			{
				return len;
			}
			else
			{
				return 0;
			}
		}
	}
	return 0;
}

int PakFile::readFile(const std::string & fileName, char ** s)
{
	if (s == NULL || fileName == "")
	{
		return 0;
	}
	
	std::string fName = fileName;
	if (fName.length() > 1 && *fName.c_str() == '\\')
	{
		fName = fName.c_str() + 1;
	}
	if (File::fileExist(fileName))
	{
		int len;
		return readFile(fileName, s, &len);
	}
	int fileID = hashFileName(fName);
	return readFile(fileID, s);
}

int PakFile::readFile(const std::string & fileName, char ** s, int * len, const std::string & pakName, bool firstReadPak)
{
	if (s == NULL || len == NULL || fileName == "")
	{
		return -1;
	}
	std::string fName = fileName;
	if (fName.length() > 1 && *fName.c_str() == '\\')
	{
		fName = fName.c_str() + 1;
	}
	int index = 0;
	FilePos filePos = findFile(fName, &index, pakName, firstReadPak);	
	if (filePos == fileInPak)
	{
		if (readPak(pakName, index, s, len))
		{
			return *len;
		}
		else
		{
			*s = NULL;
			*len = 0;
			return 0;
		}
	}
	else if (filePos == fileExists)
	{
		if (File::readFile(fName, s, len))
		{			
			return *len;
		}
		else
		{
			*s = NULL;
			*len = 0;
			return 0;
		}
	}
	else
	{
		*s = NULL;
		*len = 0;
		return -1;
	}
}

