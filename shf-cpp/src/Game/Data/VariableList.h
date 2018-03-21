#pragma once
#include "../../File/INIReader.h"
#include "../GameTypes.h"

class VariableList
{
public:
	VariableList();
	~VariableList();

	void load();
	void save();

	std::string get(const std::string & name);
	int getInteger(const std::string & name);	
	double getReal(const std::string & name);
	bool getBoolean(const std::string & name);

	void set(const std::string & name, std::string & value);
	void setInteger(const std::string & name, int value);
	void setReal(const std::string & name, double value);
	void setBoolean(const std::string & name, bool value);

private:
	virtual void freeResource();
	INIReader * ini = NULL;
};

