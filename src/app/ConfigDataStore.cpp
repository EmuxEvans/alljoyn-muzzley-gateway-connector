#include "app/ConfigDataStore.h"
#include <alljoyn/config/AboutDataStoreInterface.h>
#include <alljoyn/about/AboutServiceApi.h>
#include <alljoyn/AboutData.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <sys/stat.h>
#include <alljoyn/services_common/GuidUtil.h>

using namespace ajn;
using namespace services;

ConfigDataStore::ConfigDataStore(const char* factoryConfigFile, const char* configFile) :
    AboutDataStoreInterface(factoryConfigFile, configFile), m_IsInitialized(false), configParser(new ConfigParser(configFile))
{
    std::cout << "ConfigDataStore::AboutDataStore" << std::endl;
    m_configFileName.assign(configFile);
    m_factoryConfigFileName.assign(factoryConfigFile);

    SetNewFieldDetails("Server",      EMPTY_MASK, "s");
    SetNewFieldDetails("Port",        EMPTY_MASK, "i");
    SetNewFieldDetails("RoomJID",     EMPTY_MASK, "s");
    SetNewFieldDetails("UserJID",     EMPTY_MASK, "s");
    SetNewFieldDetails("UserPassword",EMPTY_MASK, "s");
    SetNewFieldDetails("Roster",      EMPTY_MASK, "s");
    SetNewFieldDetails("Serial",      EMPTY_MASK, "s");


    uint8_t appId[] = { 0x01, 0xB3, 0xBA, 0x14,
                        0x1E, 0x82, 0x11, 0xE4,
                        0x86, 0x51, 0xD1, 0x56,
                        0x1D, 0x5D, 0x46, 0xB0 };
    this->SetAppId(appId, 16);
    this->SetDefaultLanguage("en");
    this->SetSupportedLanguage("en");
    this->SetDeviceName("My Device Name");
    //DeviceId is a string encoded 128bit UUID
    this->SetDeviceId("93c06771-c725-48c2-b1ff-6a2a59d445b8");
    this->SetAppName("Application");
    this->SetManufacturer("Manufacturer");
    this->SetModelNumber("123456");
    this->SetDescription("A poetic description of this application");
    this->SetDateOfManufacture("2014-03-24");
    this->SetSoftwareVersion("0.1.2");
    this->SetHardwareVersion("0.0.1");
    this->SetSupportUrl("http://www.example.org");
}

void ConfigDataStore::Initialize(qcc::String deviceId, qcc::String appId)
{
    std::cout << "ConfigDataStore::Initialize " << m_configFileName << std::endl;
    std::ifstream configFile(m_configFileName.c_str(), std::ios::binary);
    std::string factoryStr((std::istreambuf_iterator<char>(configFile)),
            std::istreambuf_iterator<char>());
    std::cout << "Contains:" << std::endl << factoryStr << std::endl;

    MsgArg value; 
    std::map<std::string,std::string> configMap = configParser->GetConfigMap();
    for(std::map<std::string,std::string>::iterator it = configMap.begin(); it != configMap.end(); ++it){
        value.Set("s", it->second);
        if(it->first == "Port"){
            atoi(it->second.c_str());
        }
            this->SetField(it->first.c_str(), value);
    }

    m_IsInitialized = true;
    std::cout << "ConfigDataStore::Initialize End" << std::endl;
}

void ConfigDataStore::FactoryReset()
{
    std::cout << "ConfigDataStore::FactoryReset" << std::endl;

    m_IsInitialized = false;

    std::ifstream factoryConfigFile(m_factoryConfigFileName.c_str(), std::ios::binary);
    std::string str((std::istreambuf_iterator<char>(factoryConfigFile)),
            std::istreambuf_iterator<char>());
    factoryConfigFile.close();

    std::ofstream configFileWrite(m_configFileName.c_str(), std::ofstream::out | std::ofstream::trunc);
    configFileWrite.write(str.c_str(), str.length());
    configFileWrite.close();

    Initialize();
}

ConfigDataStore::~ConfigDataStore()
{
    std::cout << "ConfigDataStore::~AboutDataStore" << std::endl;
}

QStatus ConfigDataStore::ReadAll(const char* languageTag, DataPermission::Filter filter, ajn::MsgArg& all)
{
    QCC_UNUSED(filter);
    std::cout << "ConfigDataStore::ReadAll" << std::endl;
    QStatus status = GetAboutData(&all, languageTag);
    std::cout << "GetAboutData status = " << QCC_StatusText(status) << std::endl;
    return status;
}

QStatus ConfigDataStore::Update(const char* name, const char* languageTag, const ajn::MsgArg* value)
{
    std::cout << "ConfigDataStore::Update" << " name:" << name << " languageTag:" <<  languageTag << " value:" << value << std::endl;

    QStatus status = ER_INVALID_VALUE;
    char* chval = NULL;
    status = value->Get("s", &chval);
    if (status == ER_OK)
        status = SetField(name, *(const_cast<MsgArg*>(value)), languageTag);

    if (status == ER_OK) {
        configParser->SetField(name, chval);
        MsgArg value;
        value.Set("s", chval);
        this->SetField(name, value);

        AboutServiceApi* aboutObjApi = AboutServiceApi::getInstance();
        if (aboutObjApi) {
            status = aboutObjApi->Announce();
            std::cout << "Announce status " << QCC_StatusText(status) << std::endl;
        }
    }

    return status;
}

QStatus ConfigDataStore::Delete(const char* name, const char* languageTag)
{
    std::cout << "ConfigDataStore::Delete(" << name << ", " << languageTag << ")" << std::endl;
    QStatus status = ER_INVALID_VALUE;

    ajn::AboutData factorySettings("en");
    std::ifstream configFile(m_factoryConfigFileName.c_str(), std::ios::binary);
    if (configFile) {
        std::string str((std::istreambuf_iterator<char>(configFile)),
                std::istreambuf_iterator<char>());
        std::cout << "Contains:" << std::endl << str << std::endl;
        QStatus status;
        status = factorySettings.CreateFromXml(qcc::String(str.c_str()));

        if (status != ER_OK) {
            std::cout << "ConfigDataStore::Initialize ERROR" << std::endl;
            return status;
        }
    }

    char* tmp = NULL;
    MsgArg* value = new MsgArg;
    status = factorySettings.GetField(name, value, languageTag);
    if (status == ER_OK) {
        //status = SetSupportUrl(url);
    }

    if (status == ER_OK) {
        configParser->SetField(name, tmp);

        AboutServiceApi* aboutObjApi = AboutServiceApi::getInstance();
        if (aboutObjApi) {
            status = aboutObjApi->Announce();
            std::cout << "Announce status " << QCC_StatusText(status) << std::endl;
        }
    }

    return status;
}

const qcc::String& ConfigDataStore::GetConfigFileName()
{
    std::cout << "ConfigDataStore::GetConfigFileName" << std::endl;
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
    std::cout << "ConfigDataStore::IsLanguageSupported languageTag = " << languageTag << std::endl;
    size_t langNum;
    langNum = GetSupportedLanguages();
    std::cout << "Number of supported languages: " << langNum << std::endl;
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

    std::cout << "Returning " << QCC_StatusText(status) << std::endl;
    return status;
}
