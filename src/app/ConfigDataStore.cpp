#include "app/ConfigDataStore.h"
#include "app/ConfigParser.h"
#include "common/xmppconnutil.h"

#include <alljoyn/config/AboutDataStoreInterface.h>
#include <alljoyn/about/AboutServiceApi.h>
#include <alljoyn/AboutData.h>
#include <alljoyn/services_common/GuidUtil.h>

#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>
#include <vector>

using namespace ajn;
using namespace services;
using namespace std;

ConfigDataStore::ConfigDataStore(const char* factoryConfigFile, const char* configFile, const char* appId, const char* deviceId, RestartCallback func) :
    AboutDataStoreInterface(factoryConfigFile, configFile),
    m_IsInitialized(false), m_configFileName(configFile), m_factoryConfigFileName(factoryConfigFile),
    m_restartCallback(func), m_appId(appId), m_deviceId(deviceId)
{
    SetNewFieldDetails("Server",       REQUIRED,   "s");
    SetNewFieldDetails("UserJID",      REQUIRED,   "s");
    SetNewFieldDetails("UserPassword", REQUIRED,   "s");
    //TODO: SetNewFieldDetails("Roster",       REQUIRED,   "as");
    SetNewFieldDetails("Roster",       REQUIRED,   "s");
    SetNewFieldDetails("SerialNumber", REQUIRED,   "s");
    SetNewFieldDetails("ProductID",    REQUIRED,   "s");
    SetNewFieldDetails("Port",         EMPTY_MASK, "i");
    SetNewFieldDetails("RoomJID",      EMPTY_MASK, "s");
}

void ConfigDataStore::Initialize()
{
    ConfigParser configParser(m_configFileName.c_str());
    if(configParser.isConfigValid() == true){
        // Set the about fields
        SetDefaultLanguage("en");
        SetSupportedLanguage("en");
        SetDeviceName(configParser.GetField("DeviceName").c_str());
        LOG_VERBOSE("DeviceName: %s", configParser.GetField("DeviceName").c_str());
        SetDeviceId(m_deviceId.c_str());
        LOG_VERBOSE("DeviceId: %s", m_deviceId.c_str());
        SetAppName(configParser.GetField("AppName").c_str());
        LOG_VERBOSE("AppName: %s", configParser.GetField("AppName").c_str());
        SetManufacturer(configParser.GetField("Manufacturer").c_str());
        LOG_VERBOSE("Manufacturer: %s", configParser.GetField("Manufacturer").c_str());
        SetModelNumber(configParser.GetField("ModelNumber").c_str());
        LOG_VERBOSE("ModelNumber: %s", configParser.GetField("ModelNumber").c_str());
        SetDescription(configParser.GetField("Description").c_str());
        LOG_VERBOSE("Description: %s", configParser.GetField("Description").c_str());
        SetDateOfManufacture(configParser.GetField("DateOfManufacture").c_str());
        LOG_VERBOSE("DateOfManufacture: %s", configParser.GetField("DateOfManufacture").c_str());
        SetSoftwareVersion(configParser.GetField("SoftwareVersion").c_str());
        LOG_VERBOSE("SoftwareVersion: %s", configParser.GetField("SoftwareVersion").c_str());
        SetHardwareVersion(configParser.GetField("HardwareVersion").c_str());
        LOG_VERBOSE("HardwareVersion: %s", configParser.GetField("HardwareVersion").c_str());
        SetSupportUrl(configParser.GetField("SupportUrl").c_str());
        LOG_VERBOSE("SupportUrl: %s", configParser.GetField("SupportUrl").c_str());

        // Prime with blank values for required values that may not be in the config file
        MsgArg value;
        value.Set("s","");
        SetField("Server", value);
        SetField("UserJID", value);
        SetField("UserPassword", value);
        SetField("SerialNumber", value);
        SetField("ProductID", value);
        SetField("Roster", value);

        std::map<std::string,std::string> configMap = configParser.GetConfigMap();
        for(std::map<std::string,std::string>::iterator it = configMap.begin(); it != configMap.end(); ++it){
            if(strcmp(it->first.c_str(), "Port") == 0){
                value.Set("i", configParser.GetPort()); 
            }
            else if(strcmp(it->first.c_str(), "Roster") == 0){
                /* TODO: Use this for an array
                vector<string> roster = configParser.GetRoster();
                const char** tmp = new const char*[roster.size()];
                size_t index(0);
                for ( vector<string>::const_iterator it(roster.begin());
                    roster.end() != it; ++it, ++index )
                {
                    tmp[index] = it->c_str();
                }
                value.Set("as", roster.size(), tmp);
                delete[] tmp;
                */
                /////////////// TEMPORARY
                vector<string> roster = configParser.GetRoster();
                string firstvalue = roster.empty() ? string() : roster.front();
                value.Set("s", firstvalue.c_str());
                /////////////// END TEMPORARY
            }
            else if(strcmp(it->first.c_str(), "UserPassword") == 0){ 
                value.Set("s", "******");
            }
            else {
                value.Set("s", it->second.c_str());
            }
            SetField(it->first.c_str(), value);
        }

        SetAppId(m_appId.c_str());
        SetDeviceId(m_deviceId.c_str());
        m_IsInitialized = true;
    }
    else
    {
        LOG_RELEASE("Configuration file is not valid!")
    }
}

void ConfigDataStore::FactoryReset()
{
    m_IsInitialized = false;

    std::ifstream factoryConfigFile(m_factoryConfigFileName.c_str(), std::ios::binary);
    std::string str((std::istreambuf_iterator<char>(factoryConfigFile)),
            std::istreambuf_iterator<char>());
    factoryConfigFile.close();

    std::ofstream configFileWrite(m_configFileName.c_str(), std::ofstream::out | std::ofstream::trunc);
    configFileWrite.write(str.c_str(), str.length());
    configFileWrite.close();

    Initialize();
    m_restartCallback();
}

ConfigDataStore::~ConfigDataStore()
{
}

QStatus ConfigDataStore::ReadAll(const char* languageTag, DataPermission::Filter filter, ajn::MsgArg& all)
{
    QCC_UNUSED(filter);
    QStatus status = GetAboutData(&all, languageTag);
    return status;
}

QStatus ConfigDataStore::Update(const char* name, const char* languageTag, const ajn::MsgArg* value)
{
    QStatus status = ER_INVALID_VALUE;
    char* chval = NULL;
    int32_t intVal = 0;
    ConfigParser configParser(m_configFileName.c_str());
    if(strcmp(name, "Port") == 0){
        status = value->Get("i", &intVal);
        if (status == ER_OK) {
            MsgArg newvalue;
            configParser.SetPort(intVal);
            newvalue.Set("i", &intVal);
            SetField(name, newvalue);

            AboutServiceApi* aboutObjApi = AboutServiceApi::getInstance();
            if (aboutObjApi) {
                status = aboutObjApi->Announce();
            }
        }
    }
    else if(strcmp(name, "Roster") == 0){
        /* TODO: This will need to be an array
           int32_t numItems = 0;
           status = value->Get("as", &numItems, NULL);
           char** tmpArray = new char*[numItems];
           status = value->Get("as", &numItems, tmpArray);
           vector<string> roster;
           for ( int32_t index = 0; index < numItems; ++index )
           {
           roster.push_back(tmpArray[index]);
           }
           if(status == ER_OK){
           MsgArg newvalue;
           configParser.SetRoster( roster );
           newvalue.Set("as", numItems, tmpArray);
           SetField(name, newvalue, "en");
           }
           delete[] tmpArray;
           */
        /////////////// TEMPORARY
        status = value->Get("s", &chval);
        if (status == ER_OK) {
            // Update the config file
            vector<string> roster;
            roster.push_back(chval);
            configParser.SetRoster( roster );

            // Update our About service
            MsgArg newvalue;
            newvalue.Set("s", chval);
            SetField(name, newvalue);
        }
        /////////////// END TEMPORARY

        // Re-announce
        AboutServiceApi* aboutObjApi = AboutServiceApi::getInstance();
        if (aboutObjApi) {
            status = aboutObjApi->Announce();
        }
    }
    else if(strcmp(name, "UserPassword") == 0){
        status = value->Get("s", &chval);
        if (status == ER_OK) {
            configParser.SetField(name, chval);
        }
    }
    else{
        status = value->Get("s", &chval);
        if (status == ER_OK) {
            configParser.SetField(name, chval);

            MsgArg newvalue;
            newvalue.Set("s", chval);
            SetField(name, newvalue);

            AboutServiceApi* aboutObjApi = AboutServiceApi::getInstance();
            if (aboutObjApi) {
                status = aboutObjApi->Announce();
            }
        }
    }

    std::ifstream configFile(m_factoryConfigFileName.c_str(), std::ios::binary);
    if (configFile) {
        std::string str((std::istreambuf_iterator<char>(configFile)),
                std::istreambuf_iterator<char>());
    }
    m_restartCallback();
    return status;
}

QStatus ConfigDataStore::Delete(const char* name, const char* languageTag)
{
    ConfigParser factoryParser(m_factoryConfigFileName.c_str());
    QStatus status = ER_INVALID_VALUE;

    char* chval = NULL;
    MsgArg* value = new MsgArg;
    status = GetField(name, value, languageTag);
    if (status == ER_OK) {
        std::string tmp = factoryParser.GetField(name);
        status = value->Set("s", tmp.c_str());
        status = SetField(name, *value, languageTag);

        ConfigParser configParser(m_configFileName.c_str());
        configParser.SetField(name, tmp.c_str());

        AboutServiceApi* aboutObjApi = AboutServiceApi::getInstance();
        if (aboutObjApi) {
            status = aboutObjApi->Announce();
        }
    }
    m_restartCallback();
    return status;
}

std::string ConfigDataStore::GetConfigFileName() const
{
    return m_configFileName;
}

QStatus ConfigDataStore::IsLanguageSupported(const char* languageTag)
{
    /*
     * This looks hacky. But we need this because ER_LANGUAGE_NOT_SUPPORTED was not a part of
     * AllJoyn Core in 14.06 and is defined in alljoyn/services/about/cpp/inc/alljoyn/about/PropertyStore.h
     * with a value 0xb001 whereas in 14.12 the About support was incorporated in AllJoyn Core and
     * ER_LANGUAGE_NOT_SUPPORTED is now a part of QStatus enum with a value of 0x911a and AboutData
     * returns this if a language is not supported
     */
    QStatus status = ((QStatus)0x911a);
    size_t langNum;
    langNum = GetSupportedLanguages();
    if (langNum > 0) {
        const char** langs = new const char*[langNum];
        GetSupportedLanguages(langs, langNum);
        for (size_t i = 0; i < langNum; ++i) {
            if (0 == strcmp(languageTag, langs[i])) {
                status = ER_OK;
                break;
            }
        }
        delete [] langs;
    }
    return status;
}
