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
    AboutDataStoreInterface(factoryConfigFile, configFile), m_IsInitialized(false)
{
    std::cout << "ConfigDataStore::AboutDataStore" << std::endl;
    m_configFileName.assign(configFile);
    m_factoryConfigFileName.assign(factoryConfigFile);
    SetNewFieldDetails("SERVER",   EMPTY_MASK, "s");
    SetNewFieldDetails("CHATROOM", EMPTY_MASK, "s");
    SetNewFieldDetails("USER",     EMPTY_MASK, "s");
    SetNewFieldDetails("PASSWORD", EMPTY_MASK, "s");
    SetNewFieldDetails("RESOURCE", EMPTY_MASK, "s");
    SetNewFieldDetails("ROSTER",   EMPTY_MASK, "s");
    SetNewFieldDetails("VERBOSITY",EMPTY_MASK, "s");
    SetNewFieldDetails("COMPRESS", EMPTY_MASK, "s");
    SetNewFieldDetails("SERIAL",   EMPTY_MASK, "s");
}

void ConfigDataStore::Initialize(qcc::String deviceId, qcc::String appId)
{
    std::cout << "ConfigDataStore::Initialize " << m_configFileName << std::endl;
    std::ifstream configFile(m_configFileName.c_str(), std::ios::binary);
    QStatus status;
    if (configFile) {
        if ((!deviceId.empty()) || (!appId.empty())) {
            AboutData factoryData;
            std::ifstream factoryFile(m_factoryConfigFileName.c_str(), std::ios::binary);
            if (factoryFile) {
                std::string factoryStr((std::istreambuf_iterator<char>(factoryFile)),
                                       std::istreambuf_iterator<char>());
                std::cout << "Contains:" << std::endl << factoryStr << std::endl;
                //status = factoryData.CreateFromXml(qcc::String(factoryStr.c_str()));
                /*if (status != ER_OK) {
                    std::cout << "ConfigDataStore::Initialize ERROR" << std::endl;
                    return;
                }*/
            }

            if (!deviceId.empty()) {
                SetDeviceId(deviceId.c_str());
                factoryData.SetDeviceId(deviceId.c_str());
            }

            if (!appId.empty()) {
                SetAppId(appId.c_str());
                factoryData.SetAppId(appId.c_str());
            }

            //Generate JSON
            qcc::String str;
            //write to config file
            std::ofstream iniFileWrite(m_configFileName.c_str(), std::ofstream::out | std::ofstream::trunc);
            //write to config file
            iniFileWrite.write(str.c_str(), str.length());
            iniFileWrite.close();

            //Generate JSON
            qcc::String writeStr;
            //write to config file
            std::ofstream factoryFileWrite(m_factoryConfigFileName.c_str(), std::ofstream::out | std::ofstream::trunc);
            //write to config file
            factoryFileWrite.write(writeStr.c_str(), writeStr.length());
            factoryFileWrite.close();
        }

        size_t numFields = GetFields();
        std::cout << "ConfigDataStore::Initialize() numFields=" << numFields << std::endl;
    }

    /*if (!IsValid()) {
        std::cout << "ConfigDataStore::Initialize FAIL" << std::endl;
    } else {*/
        m_IsInitialized = true;
        std::cout << "ConfigDataStore::Initialize End" << std::endl;
    //}
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
    if (strcmp(name, AboutData::APP_ID) == 0) {
        uint8_t* appId = NULL;
        size_t* num = NULL;
        status = value->Get("ay", num, &appId);
        if (status == ER_OK) {
            status = SetAppId(appId, *num);
        }
    } else if (strcmp(name, AboutData::DEFAULT_LANGUAGE) == 0) {
        char* defaultLanguage;
        status = value->Get("s", &defaultLanguage);
        if (status == ER_OK) {
            if (0 == strcmp(defaultLanguage, "")) {
                status = ER_LANGUAGE_NOT_SUPPORTED;
            } else {
                status = IsLanguageSupported(defaultLanguage);
                if (status == ER_OK) {
                    status = SetDefaultLanguage(defaultLanguage);
                }
            }
        }
    } else if (strcmp(name, AboutData::DEVICE_NAME) == 0) {
        std::cout << "Got device name" << std::endl;
        char* deviceName = NULL;
        status = value->Get("s", &deviceName);
        status = IsLanguageSupported(languageTag);
        if (status == ER_OK) {
            status = SetDeviceName(deviceName, languageTag);
        }
    } else if (strcmp(name, AboutData::DEVICE_ID) == 0) {
        char* deviceId = NULL;
        status = value->Get("s", &deviceId);
        if (status == ER_OK) {
            status = SetDeviceId(deviceId);
        }
    } else if (strcmp(name, AboutData::APP_NAME) == 0) {
        char* appName = NULL;
        status = value->Get("s", &appName);
        if (status == ER_OK) {
            status = SetAppName(appName, languageTag);
        }
    } else if (strcmp(name, AboutData::MANUFACTURER) == 0) {
        char* chval = NULL;
        status = value->Get("s", &chval);
        if (status == ER_OK) {
            status = SetManufacturer(chval);
        }
    } else if (strcmp(name, AboutData::MODEL_NUMBER) == 0) {
        char* chval = NULL;
        status = value->Get("s", chval);
        if (status == ER_OK) {
            status = SetModelNumber(chval);
        }
    } else if (strcmp(name, AboutData::SUPPORTED_LANGUAGES) == 0) {
        //Added automatically when adding value
        std::cout << "ConfigDataStore::Update - supported languages will be added automatically when adding value" << std::endl;
    } else if (strcmp(name, AboutData::DESCRIPTION) == 0) {
        char* chval = NULL;
        status = value->Get("s", &chval);
        if (status == ER_OK) {
            status = SetDescription(chval);
        }
    } else if (strcmp(name, AboutData::DATE_OF_MANUFACTURE) == 0) {
        char* chval = NULL;
        status = value->Get("s", &chval);
        if (status == ER_OK) {
            status = SetDateOfManufacture(chval);
        }
    } else if (strcmp(name, AboutData::SOFTWARE_VERSION) == 0) {
        char* chval = NULL;
        status = value->Get("s", &chval);
        if (status == ER_OK) {
            status = SetSoftwareVersion(chval);
        }
    } else if (strcmp(name, AboutData::HARDWARE_VERSION) == 0) {
        char* chval = NULL;
        status = value->Get("s", &chval);
        if (status == ER_OK) {
            status = SetHardwareVersion(chval);
        }
    } else if (strcmp(name, AboutData::SUPPORT_URL) == 0) {
        char* chval = NULL;
        status = value->Get("s", &chval);
        if (status == ER_OK) {
            status = SetSupportUrl(chval);
        }
    } else {
        char* chval = NULL;
        status = value->Get("s", &chval);
        if (status == ER_OK)
            status = SetField(name, *(const_cast<MsgArg*>(value)), languageTag);
    }

    if (status == ER_OK) {
        //Generate xml
        qcc::String str;
        //write to config file
        std::ofstream iniFileWrite(m_configFileName.c_str(), std::ofstream::out | std::ofstream::trunc);
        //write to config file
        iniFileWrite.write(str.c_str(), str.length());
        iniFileWrite.close();

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

    if (strcmp(name, AboutData::APP_ID) == 0) {
        uint8_t* appId;
        size_t num;
        status = factorySettings.GetAppId(&appId, &num);
        if (status == ER_OK) {
            status = SetAppId(appId, num);
        }
    } else if (strcmp(name, AboutData::DEFAULT_LANGUAGE) == 0) {
        char* defaultLanguage;
        status = factorySettings.GetDefaultLanguage(&defaultLanguage);
        if (status == ER_OK) {
            status = SetDefaultLanguage(defaultLanguage);
        }
    } else if (strcmp(name, AboutData::DEVICE_NAME) == 0) {
        status = IsLanguageSupported(languageTag);
        if (status == ER_OK) {
            char* deviceName = NULL;
            status = factorySettings.GetDeviceName(&deviceName, languageTag);
            std::cout << "GetDeviceName status " << QCC_StatusText(status) << std::endl;
            if (status == ER_OK) {
                status = SetDeviceName(deviceName, languageTag);
                std::cout << "SetDeviceName status " << QCC_StatusText(status) << std::endl;
            }
        }
    } else if (strcmp(name, AboutData::DEVICE_ID) == 0) {
        char* deviceId = NULL;
        status = factorySettings.GetDeviceId(&deviceId);
        if (status == ER_OK) {
            status = SetDeviceId(deviceId);
        }
    } else if (strcmp(name, AboutData::APP_NAME) == 0) {
        char* appName;
        status = factorySettings.GetAppName(&appName);
        if (status == ER_OK) {
            status = SetAppName(appName, languageTag);
        }
    } else if (strcmp(name, AboutData::MANUFACTURER) == 0) {
        char* manufacturer = NULL;
        status = factorySettings.GetManufacturer(&manufacturer, languageTag);
        if (status == ER_OK) {
            status = SetManufacturer(manufacturer, languageTag);
        }
    } else if (strcmp(name, AboutData::MODEL_NUMBER) == 0) {
        char* modelNumber = NULL;
        status = factorySettings.GetModelNumber(&modelNumber);
        if (status == ER_OK) {
            status = SetModelNumber(modelNumber);
        }
    } else if (strcmp(name, AboutData::SUPPORTED_LANGUAGES) == 0) {
        size_t langNum;
        langNum = factorySettings.GetSupportedLanguages();
        std::cout << "Number of supported languages: " << langNum << std::endl;
        if (langNum > 0) {
            const char** langs = new const char*[langNum];
            factorySettings.GetSupportedLanguages(langs, langNum);
            for (size_t i = 0; i < langNum; ++i) {
                SetSupportedLanguage(langs[i]);
            }
        }
    } else if (strcmp(name, AboutData::DESCRIPTION) == 0) {
        char* description = NULL;
        status = factorySettings.GetDescription(&description, languageTag);
        if (status == ER_OK) {
            status = SetDescription(description, languageTag);
        }
    } else if (strcmp(name, AboutData::DATE_OF_MANUFACTURE) == 0) {
        char* date = NULL;
        status = factorySettings.GetDateOfManufacture(&date);
        if (status == ER_OK) {
            status = SetDateOfManufacture(date);
        }
    } else if (strcmp(name, AboutData::SOFTWARE_VERSION) == 0) {
        char* version = NULL;
        status = factorySettings.GetSoftwareVersion(&version);
        if (status == ER_OK) {
            status = SetSoftwareVersion(version);
        }
    } else if (strcmp(name, AboutData::HARDWARE_VERSION) == 0) {
        char* version = NULL;
        status = factorySettings.GetHardwareVersion(&version);
        if (status == ER_OK) {
            status = SetHardwareVersion(version);
        }
    } else if (strcmp(name, AboutData::SUPPORT_URL) == 0) {
        char* url = NULL;
        status = factorySettings.GetSupportUrl(&url);
        if (status == ER_OK) {
            status = SetSupportUrl(url);
        }
    } else {
        char* tmp = NULL;
        MsgArg* value = new MsgArg;
        status = factorySettings.GetField(name, value, languageTag);
        if (status == ER_OK) {
            //status = SetSupportUrl(url);
        }
    }

    if (status == ER_OK) {
        //Generate xml
        qcc::String str;
        //write to config file
        std::ofstream iniFileWrite(m_configFileName.c_str(), std::ofstream::out | std::ofstream::trunc);
        //write to config file
        iniFileWrite.write(str.c_str(), str.length());
        iniFileWrite.close();

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

void ConfigDataStore::write()
{
    //Generate xml
    qcc::String str; 
    //write to config file
    std::ofstream iniFileWrite(m_configFileName.c_str(), std::ofstream::out | std::ofstream::trunc);
    //write to config file
    iniFileWrite.write(str.c_str(), str.length());
    iniFileWrite.close();

    AboutServiceApi* aboutObjApi = AboutServiceApi::getInstance();
    if (aboutObjApi) {
        QStatus status = aboutObjApi->Announce();
        std::cout << "Announce status " << QCC_StatusText(status) << std::endl;
    }
}

/*qcc::String ConfigDataStore::ToXml(AboutData* aboutData)
{
    std::cout << "ConfigDataStore::ToXml" << std::endl;
    QStatus status = ER_OK;
    size_t numFields = aboutData->GetFields();
    if (0 == numFields) {
        std::cout << "numFields is 0" << std::endl;
        return "";
    }
    const char* fieldNames[512];
    aboutData->GetFields(fieldNames, numFields);
    char* defaultLanguage;
    status = aboutData->GetDefaultLanguage(&defaultLanguage);
    if (ER_OK != status) {
        std::cout << "GetDefaultLanguage failed" << std::endl;
        return "";
    }
    size_t numLangs = aboutData->GetSupportedLanguages();
    const char** langs = new const char*[numLangs];
    aboutData->GetSupportedLanguages(langs, numLangs);
    qcc::String res;
    res += "<AboutData>\n";
    for (size_t i = 0; i < numFields; i++) {
        ajn::MsgArg* arg;
        char* val;
        aboutData->GetField(fieldNames[i], arg);
        if (!strcmp(fieldNames[i], "AppId")) {
            res += "  <" + qcc::String(fieldNames[i]) + ">";
            size_t lay;
            uint8_t* pay;
            arg->Get("ay", &lay, &pay);
            std::stringstream ss;
            for (size_t j = 0; j < lay; ++j) {
                ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(pay[j]);
            }
            res += ss.str().c_str();
            res += "</" + qcc::String(fieldNames[i]) + ">\n";
            continue;
        }

        if (arg->Signature() != "s") {
            continue;
        }

        arg->Get("s", &val);
        res += "  <" + qcc::String(fieldNames[i]) + ">";
        res += val;
        res += "</" + qcc::String(fieldNames[i]) + ">\n";
        if (!aboutData->IsFieldLocalized(fieldNames[i])) {
            continue;
        }

        for (size_t j = 0; j < numLangs; j++) {
            if (langs[j] == defaultLanguage) {
                continue;
            }

            res += "  <" + qcc::String(fieldNames[i]) + " lang=\"" + langs[j] + "\">";
            aboutData->GetField(fieldNames[i], arg, langs[j]);
            arg->Get("s", &val);
            res += val;
            res += "</" + qcc::String(fieldNames[i]) + ">\n";
        }
    }
    res += "</AboutData>";

    delete [] langs;
    return res;
}*/

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