#include "XMPPConnector.h"                                                      // TODO: remove c-style casting
#include <vector>
#include <iostream>
#include <sstream>
#include <cerrno>
#include <stdint.h>
#include <pthread.h>

#include <alljoyn/about/AnnouncementRegistrar.h>
#include <alljoyn/services_common/GuidUtil.h>
#include <qcc/StringUtil.h>


using namespace ajn;
using namespace ajn::gw;
using namespace ajn::services;
using namespace qcc;

using std::string;
using std::vector;
using std::map;
using std::cout;
using std::endl;
using std::istringstream;
using std::ostringstream;


#define ALLJOYN_CODE_ADVERTISEMENT  "ADVERTISEMENT"
#define ALLJOYN_CODE_METHOD_CALL    "METHOD_CALL"
#define ALLJOYN_CODE_METHOD_REPLY   "METHOD_REPLY"
#define ALLJOYN_CODE_SIGNAL         "SIGNAL"
//#define ALLJOYN_CODE_NOTIFICATION   "NOTIFICATION"
#define ALLJOYN_CODE_JOIN_REQUEST   "JOIN_REQUEST"
#define ALLJOYN_CODE_JOIN_RESPONSE  "JOIN_RESPONSE"
#define ALLJOYN_CODE_SESSION_JOINED "SESSION_JOINED"
#define ALLJOYN_CODE_ANNOUNCE       "ANNOUNCE"
#define ALLJOYN_CODE_GET_PROPERTY   "GET_PROPERTY"
#define ALLJOYN_CODE_GET_PROP_REPLY "GET_PROP_REPLY"
#define ALLJOYN_CODE_SET_PROPERTY   "SET_PROPERTY"
#define ALLJOYN_CODE_SET_PROP_REPLY "SET_PROP_REPLY"
#define ALLJOYN_CODE_GET_ALL        "GET_ALL"
#define ALLJOYN_CODE_GET_ALL_REPLY  "GET_ALL_REPLY"
#define TR069_ALARM_MESSAGE         "** Alert **"


namespace util {
namespace str {

    /* Replace all occurances in 'str' of string 'from' with string 'to'. */
    static inline
    void
    ReplaceAll(
        string& str,
        string  from,
        string  to
        )
    {
        size_t pos = str.find(from);
        while(pos != string::npos)
        {
            str.replace(pos, from.length(), to);
            pos = str.find(from, pos+to.length());
        }
    }

    /* Unescape the escape sequences in an XML string. */
    static inline
    void
    UnescapeXml(
        string& str
        )
    {
        ReplaceAll(str, "&quot;", "\"");
        ReplaceAll(str, "&apos;", "'");
        ReplaceAll(str, "&lt;",   "<");
        ReplaceAll(str, "&gt;",   ">");
        ReplaceAll(str, "&amp;",  "&");
    }

    /* Trim the whitespace before and after a string. */
    string inline Trim(
        const string& str
        )
    {
        return qcc::Trim(str.c_str()).c_str();
    }

}

namespace msgarg {

    /* Convert a MsgArg into a string in XML format. */
    static
    string
    ToString(
        const MsgArg& arg,
        size_t        indent = 0
        )
    {
        string str;
    #define CHK_STR(s)  string((((s) == NULL) ? "" : (s)))
        string in(indent, ' ');

        str = in;

        indent += 2;

        switch (arg.typeId) {
        case ALLJOYN_ARRAY:
        {
            const MsgArg* elems = arg.v_array.GetElements();
            str += "<array type_sig=\"" +
                    CHK_STR(arg.v_array.GetElemSig()) + "\">";
            for (uint32_t i = 0; i < arg.v_array.GetNumElements(); i++)
            {
                str += "\n" + ToString(elems[i], indent);
            }
            str += "\n" + in + "</array>";
            break;
        }

        case ALLJOYN_BOOLEAN:
            str += arg.v_bool ? "<boolean>1</boolean>" : "<boolean>0</boolean>";
            break;

        case ALLJOYN_DOUBLE:
            // To be bit-exact stringify double as a 64-bit hex value
            str += "<double>"
                    "0x" + string(U64ToString(arg.v_uint64, 16).c_str()) +
                    "</double>";
            break;

        case ALLJOYN_DICT_ENTRY:
            str += "<dict_entry>\n" +
                   ToString(*arg.v_dictEntry.key, indent) + "\n" +
                   ToString(*arg.v_dictEntry.val, indent) + "\n" +
                   in + "</dict_entry>";
            break;

        case ALLJOYN_SIGNATURE:
            str += "<signature>" +
                    CHK_STR(arg.v_signature.sig) + "</signature>";
            break;

        case ALLJOYN_INT32:
            str += "<int32>" + string(I32ToString(arg.v_int32).c_str()) +
                    "</int32>";
            break;

        case ALLJOYN_INT16:
            str += "<int16>" + string(I32ToString(arg.v_int16).c_str()) +
                    "</int16>";
            break;

        case ALLJOYN_OBJECT_PATH:
            str += "<object_path>" +
                CHK_STR(arg.v_objPath.str) + "</object_path>";
            break;

        case ALLJOYN_UINT16:
            str += "<uint16>" + string(U32ToString(arg.v_uint16).c_str()) +
                    "</uint16>";
            break;

        case ALLJOYN_STRUCT:
            str += "<struct>\n";
            for (uint32_t i = 0; i < arg.v_struct.numMembers; i++)
            {
                str += ToString(arg.v_struct.members[i], indent) + "\n";
            }
            str += in + "</struct>";
            break;

        case ALLJOYN_STRING:
            str += "<string>" + CHK_STR(arg.v_string.str) + "</string>";
            break;

        case ALLJOYN_UINT64:
            str += "<uint64>" + string(U64ToString(arg.v_uint64).c_str()) +
                    "</uint64>";
            break;

        case ALLJOYN_UINT32:
            str += "<uint32>" + string(U32ToString(arg.v_uint32).c_str()) +
                    "</uint32>";
            break;

        case ALLJOYN_VARIANT:
            str += "<variant signature=\"" +
                    string(arg.v_variant.val->Signature().c_str()) + "\">\n";
            str += ToString(*arg.v_variant.val, indent);
            str += "\n" + in + "</variant>";
            break;

        case ALLJOYN_INT64:
            str += "<int64>" + string(I64ToString(arg.v_int64).c_str()) +
                    "</int64>";
            break;

        case ALLJOYN_BYTE:
            str += "<byte>" + string(U32ToString(arg.v_byte).c_str()) +
                    "</byte>";
            break;

        case ALLJOYN_HANDLE:
            str += "<handle>" +
                    string(BytesToHexString(
                    (const uint8_t*)&arg.v_handle.fd,
                    sizeof(arg.v_handle.fd)).c_str()) + "</handle>";
            break;

        case ALLJOYN_BOOLEAN_ARRAY:
            str += "<array type=\"boolean\">";
            if (arg.v_scalarArray.numElements)
            {
                str += "\n" + string(indent, ' ');
                for (uint32_t i = 0; i < arg.v_scalarArray.numElements; i++)
                {
                    str += arg.v_scalarArray.v_bool[i] ? "1 " : "0 ";
                }
            }
            str += "\n" + in + "</array>";
            break;

        case ALLJOYN_DOUBLE_ARRAY:
            str += "<array type=\"double\">";
            if (arg.v_scalarArray.numElements)
            {
                str += "\n" + string(indent, ' ');
                for (uint32_t i = 0; i < arg.v_scalarArray.numElements; i++)
                {
                    if(sizeof(double) == sizeof(uint64_t))
                    {
                        // To be bit-exact stringify double as 64-bit hex
                        str += "0x" + string(U64ToString(
                                *reinterpret_cast<const uint64_t*>(
                                &arg.v_scalarArray.v_double[i]), 16).c_str()) +
                                " ";
                    }
                    else
                    {
                        str += string(U64ToString(static_cast<uint64_t>(
                                arg.v_scalarArray.v_double[i])).c_str()) + " ";
                    }
                }
            }
            str += "\n" + in + "</array>";
            break;

        case ALLJOYN_INT32_ARRAY:
            str += "<array type=\"int32\">";
            if (arg.v_scalarArray.numElements)
            {
                str += "\n" + string(indent, ' ');
                for (uint32_t i = 0; i < arg.v_scalarArray.numElements; i++)
                {
                    str += string(I32ToString(
                            arg.v_scalarArray.v_int32[i]).c_str()) + " ";
                }
            }
            str += "\n" + in + "</array>";
            break;

        case ALLJOYN_INT16_ARRAY:
            str += "<array type=\"int16\">";
            if (arg.v_scalarArray.numElements)
            {
                str += "\n" + string(indent, ' ');
                for (uint32_t i = 0; i < arg.v_scalarArray.numElements; i++)
                {
                    str += string(I32ToString(
                            arg.v_scalarArray.v_int16[i]).c_str()) + " ";
                }
            }
            str += "\n" + in + "</array>";
            break;

        case ALLJOYN_UINT16_ARRAY:
            str += "<array type=\"uint16\">";
            if (arg.v_scalarArray.numElements)
            {
                str += "\n" + string(indent, ' ');
                for (uint32_t i = 0; i < arg.v_scalarArray.numElements; i++)
                {
                    str += string(U32ToString(
                            arg.v_scalarArray.v_uint16[i]).c_str()) + " ";
                }
            }
            str += "\n" + in + "</array>";
            break;

        case ALLJOYN_UINT64_ARRAY:
            str += "<array type=\"uint64\">";
            if (arg.v_scalarArray.numElements)
            {
                str += "\n" + string(indent, ' ');
                for (uint32_t i = 0; i < arg.v_scalarArray.numElements; i++)
                {
                    str += string(U64ToString(
                            arg.v_scalarArray.v_uint64[i]).c_str()) + " ";
                }
            }
            str += "\n" + in + "</array>";
            break;

        case ALLJOYN_UINT32_ARRAY:
            str += "<array type=\"uint32\">";
            if (arg.v_scalarArray.numElements)
            {
                str += "\n" + string(indent, ' ');
                for (uint32_t i = 0; i < arg.v_scalarArray.numElements; i++)
                {
                    str += string(U32ToString(
                            arg.v_scalarArray.v_uint32[i]).c_str()) + " ";
                }
            }
            str += "\n" + in + "</array>";
            break;

        case ALLJOYN_INT64_ARRAY:
            str += "<array type=\"int64\">";
            if (arg.v_scalarArray.numElements)
            {
                str += "\n" + string(indent, ' ');
                for (uint32_t i = 0; i < arg.v_scalarArray.numElements; i++)
                {
                    str += string(I64ToString(
                            arg.v_scalarArray.v_int64[i]).c_str()) + " ";
                }
            }
            str += "\n" + in + "</array>";
            break;

        case ALLJOYN_BYTE_ARRAY:
            str += "<array type=\"byte\">";
            if (arg.v_scalarArray.numElements)
            {
                str += "\n" + string(indent, ' ');
                for (uint32_t i = 0; i < arg.v_scalarArray.numElements; i++)
                {
                    str += string(U32ToString(
                            arg.v_scalarArray.v_byte[i]).c_str()) + " ";
                }
            }
            str += "\n" + in + "</array>";
            break;

        default:
            str += "<invalid/>";
            break;
        }
    #undef CHK_STR
        return str;
    }

    /* Convert a list of MsgArgs into an XML string. */
    static
    string
    ToString(
        const MsgArg* args,
        size_t        numArgs,
        size_t        indent = 0
        )
    {
        string outStr;
        for (uint32_t i = 0; i < numArgs; ++i)
        {
            outStr += ToString(args[i], indent) + '\n';
        }
        return outStr;
    }

    // Forward declaration
    static vector<MsgArg>
    VectorFromString(
        string content
        );

    /* Convert a string back into a MsgArg. */
    static
    MsgArg
    FromString(
        string argXml
        )
    {
        using str::Trim;

        MsgArg result;

        QStatus status = ER_OK;
        size_t pos = argXml.find_first_of('>')+1;
        string typeTag = Trim(argXml.substr(0, pos));
        string content = argXml.substr(pos, argXml.find_last_of('<')-pos);

        if(0 == typeTag.find("<array type_sig="))
        {
            vector<MsgArg> array = VectorFromString(content);
            status = result.Set("a*", array.size(), &array[0]);
            result.Stabilize();
        }
        else if(typeTag == "<boolean>")
        {
            status = result.Set("b", content == "1");
        }
        else if(typeTag == "<double>")
        {
            status = result.Set("d", StringToU64(content.c_str(), 16));
        }
        else if(typeTag == "<dict_entry>")
        {
            vector<MsgArg> array = VectorFromString(content);
            if(array.size() != 2)
            {
                status = ER_BUS_BAD_VALUE;
            }
            else
            {
                status = result.Set("{**}", &array[0], &array[1]);
                result.Stabilize();
            }
        }
        else if(typeTag == "<signature>")
        {
            status = result.Set("g", content.c_str());
            result.Stabilize();
        }
        else if(typeTag == "<int32>")
        {
            status = result.Set("i", StringToI32(content.c_str()));
        }
        else if(typeTag == "<int16>")
        {
            status = result.Set("n", StringToI32(content.c_str()));
        }
        else if(typeTag == "<object_path>")
        {
            status = result.Set("o", content.c_str());
            result.Stabilize();
        }
        else if(typeTag == "<uint16>")
        {
            status = result.Set("q", StringToU32(content.c_str()));
        }
        else if(typeTag == "<struct>")
        {
            vector<MsgArg> array = VectorFromString(content);
            status = result.Set("r", array.size(), &array[0]);
            result.Stabilize();
        }
        else if(typeTag == "<string>")
        {
            status = result.Set("s", content.c_str());
            result.Stabilize();
        }
        else if(typeTag == "<uint64>")
        {
            status = result.Set("t", StringToU64(content.c_str()));
        }
        else if(typeTag == "<uint32>")
        {
            status = result.Set("u", StringToU32(content.c_str()));
        }
        else if(0 == typeTag.find("<variant signature="))
        {
            MsgArg varArg = FromString(content);
            result.Set("v", &varArg);
            result.Stabilize();
        }
        else if(typeTag == "<int64>")
        {
            status = result.Set("x", StringToI64(content.c_str()));
        }
        else if(typeTag == "<byte>")
        {
            status = result.Set("y", StringToU32(content.c_str()));
        }
        else if(typeTag == "<handle>")
        {
            content = Trim(content);
            size_t len = content.length()/2;
            uint8_t* bytes = new uint8_t[len];
            if(len != HexStringToBytes(content.c_str(), bytes, len))
            {
                status = ER_BUS_BAD_VALUE;
            }
            else
            {
                status = result.Set("h", bytes);
                result.Stabilize();
            }
            delete[] bytes;
        }
        else if(typeTag == "<array type=\"boolean\">") {
            content = Trim(content);
            vector<bool> elements;
            pos = 0;
            while((pos = content.find_first_not_of(" ", pos)) != string::npos)
            {
                size_t endPos = content.find_first_of(' ', pos);
                elements.push_back(content.substr(pos, endPos-pos) == "1");
                pos = endPos;
            }

            // vector<bool> is special so we must copy it to a usable array
            bool* array = new bool[elements.size()];
            copy(elements.begin(), elements.end(), array);
            status = result.Set("ab", elements.size(), array);
            result.Stabilize();
            delete[] array;
        }
        else if(typeTag == "<array type=\"double\">") {
            content = Trim(content);
            vector<double> elements;
            pos = 0;
            while((pos = content.find_first_not_of(" ", pos)) != string::npos)
            {
                size_t endPos = content.find_first_of(' ', pos);

                if(sizeof(double) == sizeof(uint64_t))
                {
                    uint64_t val = StringToU64(
                            content.substr(pos, endPos-pos).c_str(), 16);
                    elements.push_back(*reinterpret_cast<double*>(&val));
                    pos = endPos;
                }
                else
                {
                    elements.push_back(StringToDouble(
                            content.substr(pos, endPos-pos).c_str()));
                }
            }
            status = result.Set("ad", elements.size(), &elements[0]);
            result.Stabilize();
        }
        else if(typeTag == "<array type=\"int32\">")
        {
            content = Trim(content);
            vector<int32_t> elements;
            pos = 0;
            while((pos = content.find_first_not_of(" ", pos)) != string::npos)
            {
                size_t endPos = content.find_first_of(' ', pos);
                elements.push_back(StringToI32(content.substr(
                        pos, endPos-pos).c_str()));
                pos = endPos;
            }
            status = result.Set("ai", elements.size(), &elements[0]);
            result.Stabilize();
        }
        else if(typeTag == "<array type=\"int16\">")
        {
            content = Trim(content);
            vector<int16_t> elements;
            pos = 0;
            while((pos = content.find_first_not_of(" ", pos)) != string::npos)
            {
                size_t endPos = content.find_first_of(' ', pos);
                elements.push_back(StringToI32(content.substr(
                        pos, endPos-pos).c_str()));
                pos = endPos;
            }
            status = result.Set("an", elements.size(), &elements[0]);
            result.Stabilize();
        }
        else if(typeTag == "<array type=\"uint16\">")
        {
            content = Trim(content);
            vector<uint16_t> elements;
            pos = 0;
            while((pos = content.find_first_not_of(" ", pos)) != string::npos)
            {
                size_t endPos = content.find_first_of(' ', pos);
                elements.push_back(StringToU32(content.substr(
                        pos, endPos-pos).c_str()));
                pos = endPos;
            }
            status = result.Set("aq", elements.size(), &elements[0]);
            result.Stabilize();
        }
        else if(typeTag == "<array type=\"uint64\">")
        {
            content = Trim(content);
            vector<uint64_t> elements;
            pos = 0;
            while((pos = content.find_first_not_of(" ", pos)) != string::npos)
            {
                size_t endPos = content.find_first_of(' ', pos);
                elements.push_back(StringToU64(content.substr(
                        pos, endPos-pos).c_str()));
                pos = endPos;
            }
            status = result.Set("at", elements.size(), &elements[0]);
            result.Stabilize();
        }
        else if(typeTag == "<array type=\"uint32\">")
        {
            content = Trim(content);
            vector<uint32_t> elements;
            pos = 0;
            while((pos = content.find_first_not_of(" ", pos)) != string::npos)
            {
                size_t endPos = content.find_first_of(' ', pos);
                elements.push_back(StringToU32(content.substr(
                        pos, endPos-pos).c_str()));
                pos = endPos;
            }
            status = result.Set("au", elements.size(), &elements[0]);
            result.Stabilize();
        }
        else if(typeTag == "<array type=\"int64\">")
        {
            content = Trim(content);
            vector<int64_t> elements;
            pos = 0;
            while((pos = content.find_first_not_of(" ", pos)) != string::npos)
            {
                size_t endPos = content.find_first_of(' ', pos);
                elements.push_back(StringToI64(content.substr(
                        pos, endPos-pos).c_str()));
                pos = endPos;
            }
            status = result.Set("ax", elements.size(), &elements[0]);
            result.Stabilize();
        }
        else if(typeTag == "<array type=\"byte\">")
        {
            content = Trim(content);
            vector<uint8_t> elements;
            pos = 0;
            while((pos = content.find_first_not_of(" ", pos)) != string::npos)
            {
                size_t endPos = content.find_first_of(' ', pos);
                elements.push_back(StringToU32(content.substr(
                        pos, endPos-pos).c_str()));
                pos = endPos;
            }
            status = result.Set("ay", elements.size(), &elements[0]);
            result.Stabilize();
        }

        if(status != ER_OK)
        {
            cout << "Could not create MsgArg from string: " <<
                    QCC_StatusText(status) << endl;
        }
        return result;
    }

    /* Convert a string into a vector of MsgArgs. */
    static
    vector<MsgArg>
    VectorFromString(
        string content
        )
    {
        vector<MsgArg> array;

        // Get the MsgArgs for each element
        content = str::Trim(content);
        while(!content.empty())
        {
            size_t typeBeginPos = content.find_first_of('<')+1;                 // TODO: check typeBeginPos for npos, other stuff like that in these kinds of functions
            size_t typeEndPos = content.find_first_of(" >", typeBeginPos);
            string elemType = content.substr(
                    typeBeginPos, typeEndPos-typeBeginPos);
            string closeTag = "</"+elemType+">";

            // Find the closing tag for this element
            size_t closeTagPos = content.find(closeTag);
            size_t nestedTypeEndPos = typeEndPos;
            while(closeTagPos > content.find(elemType, nestedTypeEndPos))
            {
                nestedTypeEndPos = closeTagPos+2+elemType.length();
                closeTagPos = content.find(
                        closeTag, closeTagPos+closeTag.length());
            }

            string element = content.substr(0, closeTagPos+closeTag.length());
            array.push_back(FromString(element));

            content = content.substr(closeTagPos+closeTag.length());
        }

        return array;
    }

} // namespace msgarg
} // namespace util

using namespace util;

class GenericBusAttachment :
    public BusAttachment
{
public:
    GenericBusAttachment(
        const char* applicationName
        ) :
        BusAttachment(applicationName, true),
        m_AppName(applicationName),
        m_AdvertisedName(applicationName),
        m_BusListener(0),
        m_SessionIdMap()
    {}

    ~GenericBusAttachment()
    {
        vector<BusObject*>::iterator it;
        for(it = m_BusObjects.begin(); it != m_BusObjects.end(); ++it)
        {
            delete *it;
        }

        if(m_BusListener)
        {
            UnregisterBusListener(*m_BusListener);
            delete m_BusListener;
        }
    }

    void
    AddBusListener(
        BusListener* listener
        )
    {
        RegisterBusListener(*listener);
        m_BusListener = listener;
    }

    QStatus
    AddBusObject(
        BusObject* obj
        )
    {
        QStatus err = RegisterBusObject(*obj);
        if(err == ER_OK)
        {
            m_BusObjects.push_back(obj);
        }
        return err;
    }

    void
    AddSessionIdPair(
        SessionId remoteSessionId,
        SessionId localSessionId
        )
    {
        m_SessionIdMap[remoteSessionId] = localSessionId;
    }

    SessionId
    GetRemoteSessionId(
        SessionId local
        )
    {
        map<SessionId, SessionId>::iterator it;
        for(it = m_SessionIdMap.begin(); it != m_SessionIdMap.end(); ++it)
        {
            if(it->second == local)
            {
                return it->first;
            }
        }
        return 0;
    }

    SessionId
    GetLocalSessionId(
        SessionId remote
        )
    {
        if(m_SessionIdMap.find(remote) != m_SessionIdMap.end())
        {
            return m_SessionIdMap.at(remote);
        }
        return 0;
    }

    vector<BusObject*>
    GetBusObjects()
    {
        return m_BusObjects;
    }

    BusObject*
    GetBusObject(
        string path
        )
    {
        vector<BusObject*>::iterator it;
        for(it = m_BusObjects.begin(); it != m_BusObjects.end(); ++it)
        {
            if(path == (*it)->GetPath())
            {
                return (*it);
            }
        }
        return 0;
    }

    void
    SetAdvertisedName(
        string name
        )
    {
        m_AdvertisedName = name;
    }

    string
    GetAdvertisedName()
    {
        return m_AdvertisedName;
    }

    string
    GetAppName()
    {
        return m_AppName;
    }

    BusListener*
    GetBusListener()                                                            // TODO: no point in this member being private? refactor, get rid of type-casting everywhere
    {
        return m_BusListener;
    }

private:
    string m_AppName;
    string m_AdvertisedName;
    vector<BusObject*> m_BusObjects;
    BusListener* m_BusListener;

    map<SessionId, SessionId> m_SessionIdMap;
};

class AllJoynHandler :
    public BusListener,
    public SessionPortListener,
    public ProxyBusObject::Listener,
    public AnnounceHandler
{
public:
    AllJoynHandler(
        XMPPConnector* connector,
        xmpp_conn_t*   xmppConn
        ) :
        BusListener(),
        m_Connector(connector),
        m_XmppConn(xmpp_conn_clone(xmppConn)),
        m_AssociatedBus(0),
        m_SessionJoinedSignalReceived(false),
        m_RemoteSessionId(0)
    {
        pthread_mutex_init(&m_SessionJoinedMutex, NULL);
        pthread_cond_init(&m_SessionJoinedWaitCond, NULL);
    }

    virtual
    ~AllJoynHandler()
    {
        xmpp_conn_release(m_XmppConn);
        m_XmppConn = 0;
        pthread_mutex_destroy(&m_SessionJoinedMutex);
        pthread_cond_destroy(&m_SessionJoinedWaitCond);
    }

    void
    FoundAdvertisedName(
        const char*   name,
        TransportMask transport,
        const char*   namePrefix
        )
    {
        if(m_AssociatedBus)
        {
            // This means we are on a proxy bus attachment.                     // TODO: separate classes for primary AJHandler (BusListener) and proxy ones, m_AssociatedBus and certain callbacks only apply to one or the other
            // We shouldn't even be looking for advertised names here.
            return;
        }

        if(name == strstr(name, "org.alljoyn.BusNode"))
        {
            return;
        }

        // Check to see if we are advertising this name
        if(m_Connector->IsAdvertisingName(name))
        {
            return;
        }

        cout << "Found advertised name: " << name << endl;

        ProxyBusObject* proxy = new ProxyBusObject(
                *(m_Connector->GetBusAttachment()), name, "/", 0);
        QStatus err = proxy->IntrospectRemoteObjectAsync(
                this, static_cast<ProxyBusObject::Listener::IntrospectCB>(
                &AllJoynHandler::IntrospectCallback), proxy);
        if(err != ER_OK)
        {
            cout << "Could not introspect remote object: " <<
                    QCC_StatusText(err) << endl;
        }
    }

    void
    LostAdvertisedName(
        const char*   name,
        TransportMask transport,
        const char*   namePrefix
        )
    {
                                                                                // TODO: send via XMPP, on receipt should stop advertising and unregister/delete associated bus objects
    }

    /*void
    NameOwnerChanged(
        const char* busName,
        const char* previousOwner,
        const char* newOwner
        )
    {
        if(!newOwner)
        {
            return;
        }

        m_Connector->GetBusAttachment()->EnableConcurrentCallbacks();

        cout << "NameOwnerChanged: " << newOwner << endl;
        vector<XMPPConnector::RemoteBusObject> joiner_ifaces;
        ProxyBusObject proxy(
            *(m_Connector->GetBusAttachment()), newOwner, "/", 0);
        getInterfacesFromAdvertisedNameRecursive(joiner_ifaces, NULL, proxy);
    }*/

    bool
    AcceptSessionJoiner(
        SessionPort sessionPort,
        const char* joiner,
        const       SessionOpts& opts
        )
    {
        cout << "Session join requested." << endl;

        // We don't know what bus attachment this is for,
        //  do not accept the session.
        if(!m_AssociatedBus)                                                    // TODO: part of splitting class for proxy attachments from primary attachment, will have no need for these kinds of checks
        {
            return false;
        }

        m_AssociatedBus->EnableConcurrentCallbacks();

        // Lock the session join mutex
        pthread_mutex_lock(&m_SessionJoinedMutex);
        m_SessionJoinedSignalReceived = false;
        m_RemoteSessionId = 0;

        // Pack the interfaces in an XMPP message and send them to the server
        xmpp_stanza_t* message =
                xmpp_stanza_new(xmpp_conn_get_context(m_XmppConn));
        CreateXmppSessionJoinerStanza(sessionPort, joiner, opts, message);

        char* buf = 0;
        size_t buflen = 0;
        xmpp_stanza_to_text(message, &buf, &buflen);
        cout << "Sending XMPP session join request message." << endl;
        free(buf);

        xmpp_send(m_XmppConn, message);                                         // TODO: make a transport class for XMPP stanza/conn/ctx handling, sending/receiving (take from XMPPConnector class)
        xmpp_stanza_release(message);

        // Wait for the XMPP response signal
        struct timespec wait_time;
        wait_time.tv_sec = time(NULL)+10; wait_time.tv_nsec = 0;
        while(!m_SessionJoinedSignalReceived)
        {
            if(ETIMEDOUT == pthread_cond_timedwait(
                    &m_SessionJoinedWaitCond,
                    &m_SessionJoinedMutex,
                    &wait_time))
            {
                break;
            }
        }

        bool returnVal = (m_RemoteSessionId != 0);

        pthread_mutex_unlock(&m_SessionJoinedMutex);

        return returnVal;
    }

    void
    SessionJoined(
        SessionPort sessionPort,
        SessionId   id,
        const char* joiner)
    {
        if(m_AssociatedBus)
        {
            m_AssociatedBus->AddSessionIdPair(m_RemoteSessionId, id);           // TODO: race condition using m_RemoteSessionId, support 2 sessions joining simultaneously (could map joiner to remote id?)
        }

        // Send the session Id back across the XMPP server
        xmpp_stanza_t* message =
                xmpp_stanza_new(xmpp_conn_get_context(m_XmppConn));
        CreateXmppSessionJoinedStanza(sessionPort, id, joiner, message);

        char* buf = 0;
        size_t buflen = 0;
        xmpp_stanza_to_text(message, &buf, &buflen);
        cout << "Sending XMPP session joined message" << endl;
        free(buf);

        xmpp_send(m_XmppConn, message);
        xmpp_stanza_release(message);
    }

    void
    HandleAllJoynMethodCall(
        const InterfaceDescription::Member* member,
        Message&                            message,
        string                              destName,
        string                              destPath
        )
    {
        // Send the method call via XMPP
        xmpp_stanza_t* stanza =
                xmpp_stanza_new(xmpp_conn_get_context(m_XmppConn));
        CreateXmppMethodCallStanza(member, message, destName, destPath, stanza);

        char* buf = 0;
        size_t buflen = 0;
        xmpp_stanza_to_text(stanza, &buf, &buflen);
        cout << "Sending XMPP method call message" << endl;
        free(buf);

        xmpp_send(m_XmppConn, stanza);
        xmpp_stanza_release(stanza);
    }

    void
    HandleAllJoynGetRequest(
        string ifcName,
        string propName,
        string destName,
        string destPath
        )
    {
        // Send the method call via XMPP
        xmpp_stanza_t* stanza =
                xmpp_stanza_new(xmpp_conn_get_context(m_XmppConn));
        CreateXmppGetRequestStanza(
                ifcName, propName, destName, destPath, stanza);

        char* buf = 0;
        size_t buflen = 0;
        xmpp_stanza_to_text(stanza, &buf, &buflen);
        cout << "Sending XMPP Get request message" << endl;
        free(buf);

        xmpp_send(m_XmppConn, stanza);
        xmpp_stanza_release(stanza);
    }

    void
    HandleAllJoynGetAllRequest(
        const InterfaceDescription::Member* member,
        string                              destName,
        string                              destPath
        )
    {
        // Send the method call via XMPP
        xmpp_stanza_t* stanza =
                xmpp_stanza_new(xmpp_conn_get_context(m_XmppConn));
        CreateXmppGetAllRequestStanza(member, destName, destPath, stanza);

        char* buf = 0;
        size_t buflen = 0;
        xmpp_stanza_to_text(stanza, &buf, &buflen);
        cout << "Sending XMPP Get All request message" << endl;
        free(buf);

        xmpp_send(m_XmppConn, stanza);
        xmpp_stanza_release(stanza);
    }

    void
    AllJoynSignalHandler(
        const InterfaceDescription::Member* member,
        const char*                         srcPath,
        Message&                            message
        )
    {
        // Pack the signal in an XMPP message and send it to the server
        xmpp_stanza_t* xmpp_message =
                xmpp_stanza_new(xmpp_conn_get_context(m_XmppConn));
        CreateXmppSignalStanza(member, srcPath, message, xmpp_message);

        char* buf = 0;
        size_t buflen = 0;
        xmpp_stanza_to_text(xmpp_message, &buf, &buflen);
        cout << "Sending XMPP AJ signal message:\n" << buf << endl;
        free(buf);

        xmpp_send(m_XmppConn, xmpp_message);
        xmpp_stanza_release(xmpp_message);
    }

    void
    AssociateBusAttachment(
        GenericBusAttachment* bus
        )
    {
        m_AssociatedBus = bus;
    }

    void
    SignalSessionJoined(
        SessionId result
        )
    {
        pthread_mutex_lock(&m_SessionJoinedMutex);
        m_SessionJoinedSignalReceived = true;
        m_RemoteSessionId = result;
        pthread_cond_signal(&m_SessionJoinedWaitCond);
        pthread_mutex_unlock(&m_SessionJoinedMutex);
    }

    void
    Announce(
        uint16_t                  version,
        uint16_t                  port,
        const char*               busName,
        const ObjectDescriptions& objectDescs,
        const AboutData&          aboutData
        )
    {
        // Send the announcement along
        xmpp_stanza_t* message =
                xmpp_stanza_new(xmpp_conn_get_context(m_XmppConn));
        CreateXmppAnnounceStanza(
                version, port, busName, objectDescs, aboutData, message);

        char* buf = 0;
        size_t buflen = 0;
        xmpp_stanza_to_text(message, &buf, &buflen);
        cout << "Sending XMPP Announce message:\n" << buf << endl;
        free(buf);

        xmpp_send(m_XmppConn, message);
        xmpp_stanza_release(message);
    }

public:
    static
    void
    GetInterfacesRecursive(
        vector<XMPPConnector::RemoteBusObject>& ifaces,
        ProxyBusObject&                         proxy
        )
    {
        QStatus err = proxy.IntrospectRemoteObject(500);
        if(err != ER_OK)
        {
            return;
        }

        XMPPConnector::RemoteBusObject this_bus_object;
        this_bus_object.objectPath = proxy.GetPath().c_str();
        if(this_bus_object.objectPath.empty()) {
            this_bus_object.objectPath = "/";
        }

        // Get the interfaces implemented at this object path
        size_t num_ifaces = proxy.GetInterfaces();
        if(num_ifaces != 0)
        {
            InterfaceDescription** iface_list =
                    new InterfaceDescription*[num_ifaces];
            num_ifaces = proxy.GetInterfaces(
                    (const InterfaceDescription**)iface_list, num_ifaces);

            // Find the interface(s) being advertised by this AJ device
            for(uint32_t i = 0; i < num_ifaces; ++i)
            {
                const char* iface_name = iface_list[i]->GetName();
                string iface_name_str(iface_name);
                if(iface_name_str != "org.freedesktop.DBus.Peer"           &&
                   iface_name_str != "org.freedesktop.DBus.Introspectable" &&
                   iface_name_str != "org.freedesktop.DBus.Properties"     &&
                   iface_name_str != "org.allseen.Introspectable"          )
                {
                    this_bus_object.interfaces.push_back(iface_list[i]);
                }
            }

            delete[] iface_list;

            if(!this_bus_object.interfaces.empty())
            {
                ifaces.push_back(this_bus_object);
            }
        }

        // Get the children of this object path
        size_t num_children = proxy.GetChildren();
        if(num_children != 0)
        {
            ProxyBusObject** children = new ProxyBusObject*[num_children];
            num_children = proxy.GetChildren(children, num_children);

            for(uint32_t i = 0; i < num_children; ++i)
            {
                GetInterfacesRecursive(ifaces, *children[i]);
            }

            delete[] children;
        }
    }

private:
    void
    IntrospectCallback(
        QStatus         status,
        ProxyBusObject* obj,
        void*           context
        )
    {
        ProxyBusObject* proxy = (ProxyBusObject*)context;

        // Get the interfaces implemented by this advertised name
        BusAttachment* bus = m_Connector->GetBusAttachment();
        bus->EnableConcurrentCallbacks();

        pthread_mutex_lock(&m_SessionJoinedMutex);                              // TODO: different mutex, need a mutex at all?
        cout << "Introspect callback on " <<
                proxy->GetServiceName().c_str() << endl;

        vector<XMPPConnector::RemoteBusObject> busObjects;
        GetInterfacesRecursive(busObjects, *proxy);

        if(!busObjects.empty())
        {
            // Pack the interfaces in an XMPP message
            xmpp_stanza_t* message =
                    xmpp_stanza_new(xmpp_conn_get_context(m_XmppConn));
            CreateXmppInterfaceStanza(
                    proxy->GetServiceName().c_str(), busObjects, message);

            char* buf = 0;
            size_t buflen = 0;
            xmpp_stanza_to_text(message, &buf, &buflen);
            cout << "Sending XMPP advertise message" << endl;
            free(buf);

            xmpp_send(m_XmppConn, message);
            xmpp_stanza_release(message);
        }
        pthread_mutex_unlock(&m_SessionJoinedMutex);

        delete proxy;
    }

    void
    CreateXmppInterfaceStanza(
        const char*                                   advertisedName,
        const vector<XMPPConnector::RemoteBusObject>& busObjects,
        xmpp_stanza_t*                                stanza
        )
    {
        // Construct the text that will be the body of our message
        ostringstream msg_stream;
        msg_stream << ALLJOYN_CODE_ADVERTISEMENT << "\n";
        msg_stream << advertisedName << "\n\n";
        vector<XMPPConnector::RemoteBusObject>::const_iterator it;
        for(it = busObjects.begin(); it != busObjects.end(); ++it)
        {
            msg_stream << it->objectPath << "\n";
            vector<const InterfaceDescription*>::const_iterator if_it;
            for(if_it = it->interfaces.begin();
                if_it != it->interfaces.end();
                ++if_it)
            {
                msg_stream << (*if_it)->GetName() << "\n";
                msg_stream << (*if_it)->Introspect().c_str() << "\n";
            }

            msg_stream << "\n";
        }

        // Now wrap it in an XMPP stanza
        xmpp_ctx_t* xmppCtx = xmpp_conn_get_context(m_XmppConn);

        xmpp_stanza_set_name(stanza, "message");
        xmpp_stanza_set_attribute(
                stanza, "to", m_Connector->GetChatroomJabberId().c_str());
        xmpp_stanza_set_type(stanza, "groupchat");

        xmpp_stanza_t* body = xmpp_stanza_new(xmppCtx);
        xmpp_stanza_set_name(body, "body");
        xmpp_stanza_t* text = xmpp_stanza_new(xmppCtx);
        xmpp_stanza_set_text(text, msg_stream.str().c_str());
        xmpp_stanza_add_child(body, text);
        xmpp_stanza_release(text);
        xmpp_stanza_add_child(stanza, body);
        xmpp_stanza_release(body);
    }

    void
    CreateXmppSessionJoinerStanza(
        SessionPort        sessionPort,
        const char*        joiner,
        const SessionOpts& opts,
        xmpp_stanza_t*     stanza
        )
    {
        // Construct the text that will be the body of our message
        ostringstream msg_stream;
        msg_stream << ALLJOYN_CODE_JOIN_REQUEST << "\n";
        msg_stream << m_AssociatedBus->GetAppName() << "\n";
        msg_stream << sessionPort << "\n";
        msg_stream << joiner << "\n";

        // Gather interfaces so that they may be implemented on the remote end
        vector<XMPPConnector::RemoteBusObject> joiner_objects;
        ProxyBusObject proxy(*m_AssociatedBus, joiner, "/", 0);
        GetInterfacesRecursive(joiner_objects, proxy);

        vector<XMPPConnector::RemoteBusObject>::const_iterator it;
        for(it = joiner_objects.begin(); it != joiner_objects.end(); ++it)
        {
            msg_stream << it->objectPath << "\n";
            vector<const InterfaceDescription*>::const_iterator if_it;
            for(if_it = it->interfaces.begin();
                if_it != it->interfaces.end();
                ++if_it)
            {
                msg_stream << (*if_it)->GetName() << "\n";
                msg_stream << (*if_it)->Introspect().c_str() << "\n";
            }

            msg_stream << "\n";
        }

        // Now wrap it in an XMPP stanza
        xmpp_ctx_t* xmppCtx = xmpp_conn_get_context(m_XmppConn);

        xmpp_stanza_set_name(stanza, "message");
        xmpp_stanza_set_attribute(
                stanza, "to", m_Connector->GetChatroomJabberId().c_str());
        xmpp_stanza_set_type(stanza, "groupchat");

        xmpp_stanza_t* body = xmpp_stanza_new(xmppCtx);
        xmpp_stanza_set_name(body, "body");
        xmpp_stanza_t* text = xmpp_stanza_new(xmppCtx);
        xmpp_stanza_set_text(text, msg_stream.str().c_str());
        xmpp_stanza_add_child(body, text);
        xmpp_stanza_release(text);
        xmpp_stanza_add_child(stanza, body);
        xmpp_stanza_release(body);
    }

    void
    CreateXmppSessionJoinedStanza(
        SessionPort    sessionPort,
        SessionId      id,
        const char*    joiner,
        xmpp_stanza_t* stanza
        )
    {
        // Construct the text that will be the body of our message
        ostringstream msg_stream;
        msg_stream << ALLJOYN_CODE_SESSION_JOINED << "\n";
        msg_stream << joiner << "\n";
        msg_stream << m_AssociatedBus->GetRemoteSessionId(id) << "\n";
        msg_stream << id << "\n";

        // Now wrap it in an XMPP stanza
        xmpp_ctx_t* xmppCtx = xmpp_conn_get_context(m_XmppConn);

        xmpp_stanza_set_name(stanza, "message");
        xmpp_stanza_set_attribute(
                stanza, "to", m_Connector->GetChatroomJabberId().c_str());
        xmpp_stanza_set_type(stanza, "groupchat");

        xmpp_stanza_t* body = xmpp_stanza_new(xmppCtx);
        xmpp_stanza_set_name(body, "body");
        xmpp_stanza_t* text = xmpp_stanza_new(xmppCtx);
        xmpp_stanza_set_text(text, msg_stream.str().c_str());
        xmpp_stanza_add_child(body, text);
        xmpp_stanza_release(text);
        xmpp_stanza_add_child(stanza, body);
        xmpp_stanza_release(body);
    }

    void
    CreateXmppAnnounceStanza(
        uint16_t                  version,
        uint16_t                  port,
        const char*               busName,
        const ObjectDescriptions& objectDescs,
        const AboutData&          aboutData,
        xmpp_stanza_t*            stanza
        )
    {
        // Construct the text that will be the body of our message
        ostringstream msg_stream;
        msg_stream << ALLJOYN_CODE_ANNOUNCE << "\n";
        msg_stream << version << "\n";
        msg_stream << port << "\n";
        msg_stream << busName << "\n";

        ObjectDescriptions::const_iterator it;
        for(it = objectDescs.begin(); it != objectDescs.end(); ++it)
        {
            msg_stream << it->first.c_str() << "\n";
            vector<String>::const_iterator val_iter;
            for(val_iter = it->second.begin();
                val_iter != it->second.end();
                ++val_iter)
            {
                msg_stream << val_iter->c_str() << "\n";
            }
        }

        msg_stream << "\n";

        AboutData::const_iterator about_it;
        for(about_it = aboutData.begin();
            about_it != aboutData.end();
            ++about_it)
        {
            msg_stream << about_it->first.c_str() << "\n";
            msg_stream << msgarg::ToString(about_it->second) << "\n\n";
        }

        // Now wrap it in an XMPP stanza
        xmpp_ctx_t* xmppCtx = xmpp_conn_get_context(m_XmppConn);

        xmpp_stanza_set_name(stanza, "message");
        xmpp_stanza_set_attribute(
                stanza, "to", m_Connector->GetChatroomJabberId().c_str());
        xmpp_stanza_set_type(stanza, "groupchat");

        xmpp_stanza_t* body = xmpp_stanza_new(xmppCtx);
        xmpp_stanza_set_name(body, "body");
        xmpp_stanza_t* text = xmpp_stanza_new(xmppCtx);
        xmpp_stanza_set_text(text, msg_stream.str().c_str());
        xmpp_stanza_add_child(body, text);
        xmpp_stanza_release(text);
        xmpp_stanza_add_child(stanza, body);
        xmpp_stanza_release(body);
    }

    void
    CreateXmppMethodCallStanza(
        const InterfaceDescription::Member* member,
        Message&                            message,
        string                              destName,
        string                              destPath,
        xmpp_stanza_t*                      stanza
        )
    {
        size_t num_args = 0;
        const MsgArg* msgargs = 0;
        message->GetArgs(num_args, msgargs);

        // Construct the text that will be the body of our message
        ostringstream msg_stream;
        msg_stream << ALLJOYN_CODE_METHOD_CALL << "\n";
        msg_stream << message->GetSender() << "\n";
        msg_stream << destName << "\n";
        msg_stream << destPath << "\n";
        msg_stream << member->iface->GetName() << "\n";
        msg_stream << member->name << "\n";
        msg_stream << message->GetSessionId() << "\n";
        msg_stream << msgarg::ToString(msgargs, num_args) << "\n";


        // Now wrap it in an XMPP stanza
        xmpp_ctx_t* xmppCtx = xmpp_conn_get_context(m_XmppConn);

        xmpp_stanza_set_name(stanza, "message");
        xmpp_stanza_set_attribute(
                stanza, "to", m_Connector->GetChatroomJabberId().c_str());
        xmpp_stanza_set_type(stanza, "groupchat");

        xmpp_stanza_t* body = xmpp_stanza_new(xmppCtx);
        xmpp_stanza_set_name(body, "body");
        xmpp_stanza_t* text = xmpp_stanza_new(xmppCtx);
        xmpp_stanza_set_text(text, msg_stream.str().c_str());
        xmpp_stanza_add_child(body, text);
        xmpp_stanza_release(text);
        xmpp_stanza_add_child(stanza, body);
        xmpp_stanza_release(body);
    }

    void
    CreateXmppSignalStanza(
        const InterfaceDescription::Member* member,
        const char*                         srcPath,
        Message&                            message,
        xmpp_stanza_t*                      stanza
        )
    {
        size_t num_args = 0;
        const MsgArg* msgargs = 0;
        message->GetArgs(num_args, msgargs);

        // Construct the text that will be the body of our message
        ostringstream msg_stream;
        msg_stream << ALLJOYN_CODE_SIGNAL << "\n";
        msg_stream << message->GetSender() << "\n";
        msg_stream << message->GetDestination() << "\n";
        msg_stream << message->GetSessionId() << "\n";
        msg_stream << member->iface->GetName() << "\n";
        msg_stream << member->name << "\n";
        msg_stream << msgarg::ToString(msgargs, num_args) << "\n";


        // Now wrap it in an XMPP stanza
        xmpp_ctx_t* xmppCtx = xmpp_conn_get_context(m_XmppConn);

        xmpp_stanza_set_name(stanza, "message");
        xmpp_stanza_set_attribute(
                stanza, "to", m_Connector->GetChatroomJabberId().c_str());
        xmpp_stanza_set_type(stanza, "groupchat");

        xmpp_stanza_t* body = xmpp_stanza_new(xmppCtx);
        xmpp_stanza_set_name(body, "body");
        xmpp_stanza_t* text = xmpp_stanza_new(xmppCtx);
        xmpp_stanza_set_text(text, msg_stream.str().c_str());
        xmpp_stanza_add_child(body, text);
        xmpp_stanza_release(text);
        xmpp_stanza_add_child(stanza, body);
        xmpp_stanza_release(body);
    }

    void
    CreateXmppGetRequestStanza(
        string         ifcName,
        string         propName,
        string         destName,
        string         destPath,
        xmpp_stanza_t* stanza
        )
    {
        // Construct the text that will be the body of our message
        ostringstream msg_stream;
        msg_stream << ALLJOYN_CODE_GET_PROPERTY << "\n";
        msg_stream << destName << "\n";
        msg_stream << destPath << "\n";
        msg_stream << ifcName << "\n";
        msg_stream << propName << "\n";

        // Now wrap it in an XMPP stanza
        xmpp_ctx_t* xmppCtx = xmpp_conn_get_context(m_XmppConn);

        xmpp_stanza_set_name(stanza, "message");
        xmpp_stanza_set_attribute(
                stanza, "to", m_Connector->GetChatroomJabberId().c_str());
        xmpp_stanza_set_type(stanza, "groupchat");

        xmpp_stanza_t* body = xmpp_stanza_new(xmppCtx);
        xmpp_stanza_set_name(body, "body");
        xmpp_stanza_t* text = xmpp_stanza_new(xmppCtx);
        xmpp_stanza_set_text(text, msg_stream.str().c_str());
        xmpp_stanza_add_child(body, text);
        xmpp_stanza_release(text);
        xmpp_stanza_add_child(stanza, body);
        xmpp_stanza_release(body);
    }

    void
    CreateXmppGetAllRequestStanza(
        const InterfaceDescription::Member* member,
        string                              destName,
        string                              destPath,
        xmpp_stanza_t*                      stanza
        )
    {
        // Construct the text that will be the body of our message
        ostringstream msg_stream;
        msg_stream << ALLJOYN_CODE_GET_ALL << "\n";
        msg_stream << destName << "\n";
        msg_stream << destPath << "\n";
        msg_stream << member->iface->GetName() << "\n";
        msg_stream << member->name << "\n";

        // Now wrap it in an XMPP stanza
        xmpp_ctx_t* xmppCtx = xmpp_conn_get_context(m_XmppConn);

        xmpp_stanza_set_name(stanza, "message");
        xmpp_stanza_set_attribute(
                stanza, "to", m_Connector->GetChatroomJabberId().c_str());
        xmpp_stanza_set_type(stanza, "groupchat");

        xmpp_stanza_t* body = xmpp_stanza_new(xmppCtx);
        xmpp_stanza_set_name(body, "body");
        xmpp_stanza_t* text = xmpp_stanza_new(xmppCtx);
        xmpp_stanza_set_text(text, msg_stream.str().c_str());
        xmpp_stanza_add_child(body, text);
        xmpp_stanza_release(text);
        xmpp_stanza_add_child(stanza, body);
        xmpp_stanza_release(body);
    }

    XMPPConnector* m_Connector;
    xmpp_conn_t* m_XmppConn;
    GenericBusAttachment* m_AssociatedBus;

    bool m_SessionJoinedSignalReceived;
    SessionId m_RemoteSessionId;
    pthread_mutex_t m_SessionJoinedMutex;
    pthread_cond_t m_SessionJoinedWaitCond;
};

class GenericBusObject :
    public BusObject
{
public:
    GenericBusObject(
        string objectPath
        ) :
        BusObject(objectPath.c_str()),
        m_ReplyReceived(false),
        m_ReplyStr(""),
        m_Interfaces()
    {
        pthread_mutex_init(&m_ReplyReceivedMutex, NULL);
        pthread_cond_init(&m_ReplyReceivedWaitCond, NULL);
    }

    virtual
    ~GenericBusObject()
    {
        pthread_mutex_destroy(&m_ReplyReceivedMutex);
        pthread_cond_destroy(&m_ReplyReceivedWaitCond);
    }

    void
    AllJoynMethodHandler(
        const InterfaceDescription::Member* member,
        Message&                            message
        )
    {
        size_t num_args = 0;
        const MsgArg* msgargs = 0;
        message->GetArgs(num_args, msgargs);
        cout << "Received method call: " << member->name << endl;
        cout << "sig: " << member->signature << endl;
        cout << "return sig: " << member->returnSignature << endl;

        if(!bus)
        {
            return;
        }

        pthread_mutex_lock(&m_ReplyReceivedMutex);
        m_ReplyReceived = false;
        m_ReplyStr = "";

        ((AllJoynHandler*)((GenericBusAttachment*)bus)->GetBusListener())->
                HandleAllJoynMethodCall(member, message,
                ((GenericBusAttachment*)bus)->GetAppName(), this->GetPath());

        // Wait for the XMPP response signal
        struct timespec wait_time;
        wait_time.tv_sec = time(NULL)+10; wait_time.tv_nsec = 0;
        while(!m_ReplyReceived)
        {
            if(ETIMEDOUT == pthread_cond_timedwait(
                    &m_ReplyReceivedWaitCond,
                    &m_ReplyReceivedMutex,
                    &wait_time))
            {
                break;
            }
        }

        string replyStr = m_ReplyStr;

        pthread_mutex_unlock(&m_ReplyReceivedMutex);

        vector<MsgArg> replyArgs = msgarg::VectorFromString(replyStr);
        QStatus err = MethodReply(message, &replyArgs[0], replyArgs.size());
        if(err != ER_OK)
        {
            cout << "Failed to reply to method call: " <<
                    QCC_StatusText(err) << endl;
        }
    }

    void
    AllJoynSignalHandler(
        const InterfaceDescription::Member* member,
        const char*                         srcPath,
        Message&                            message
        )
    {
        size_t num_args = 0;
        const MsgArg* msgargs = 0;
        message->GetArgs(num_args, msgargs);
        cout << "Received signal: " <<
                msgarg::ToString(msgargs, num_args) << endl;

        // TODO: handle signals
    }

    void
    ObjectRegistered()
    {
        cout << ((GenericBusAttachment*)bus)->GetAdvertisedName() <<
                GetPath() << " registered" << endl;
    }

    QStatus
    ImplementInterfaces(
        vector<const InterfaceDescription*> interfaces,
        BusAttachment*                      busAttachment)
    {
        vector<const InterfaceDescription*>::iterator it;
        for(it = interfaces.begin(); it != interfaces.end(); ++it)
        {
            QStatus err = AddInterface(**it);
            if(ER_OK != err)
            {
                return err;
            }

            m_Interfaces.push_back(*it);

            // Register method handlers
            size_t num_members = (*it)->GetMembers();
            InterfaceDescription::Member** interface_members =
                    new InterfaceDescription::Member*[num_members];
            num_members = (*it)->GetMembers(
                    (const InterfaceDescription::Member**)interface_members,
                    num_members);

            for(uint32_t i = 0; i < num_members; ++i)
            {
                if(interface_members[i]->memberType == MESSAGE_SIGNAL)
                {
                    /*err = busAttachment->RegisterSignalHandler(this,
                            static_cast<MessageReceiver::SignalHandler>(
                            &GenericBusObject::AllJoynSignalHandler),
                            interface_members[i], NULL);*/
                }
                else
                {
                    err = AddMethodHandler(interface_members[i],
                            static_cast<MessageReceiver::MethodHandler>(
                            &GenericBusObject::AllJoynMethodHandler));
                }
                if(err != ER_OK)
                {
                    cout << "Failed to add method handler for " <<
                            interface_members[i]->name.c_str() << ": " <<
                            QCC_StatusText(err) << endl;
                }
            }

            delete[] interface_members;
        }

        return ER_OK;
    }

    QStatus
    Get(
        const char* ifcName,
        const char* propName,
        MsgArg&     val
        )
    {
        cout << "Received alljoyn get request for " << ifcName << ":" <<
                propName << endl;

        if(!bus)
        {
            return ER_BUS_NO_SUCH_PROPERTY;
        }

        pthread_mutex_lock(&m_ReplyReceivedMutex);
        m_ReplyReceived = false;
        m_ReplyStr = "";

        ((AllJoynHandler*)((GenericBusAttachment*)bus)->GetBusListener())->
                HandleAllJoynGetRequest(ifcName, propName,
                ((GenericBusAttachment*)bus)->GetAppName(), this->GetPath());

        // Wait for the XMPP response signal
        struct timespec wait_time;
        wait_time.tv_sec = time(NULL)+10; wait_time.tv_nsec = 0;
        while(!m_ReplyReceived)
        {
            if(ETIMEDOUT == pthread_cond_timedwait(
                    &m_ReplyReceivedWaitCond,
                    &m_ReplyReceivedMutex,
                    &wait_time))
            {
                break;
            }
        }

        bool replyReceived = m_ReplyReceived;
        string replyStr = m_ReplyStr;

        pthread_mutex_unlock(&m_ReplyReceivedMutex);

        if(replyReceived)
        {
            MsgArg why(msgarg::FromString(replyStr));
            if(why.Signature() == "v") {
                val = *why.v_variant.val;                                       // TODO: why do I have to do this?
            } else {
                val = why;
            }
            val.Stabilize();
            return ER_OK;
        }
        else
        {
            return ER_BUS_NO_SUCH_PROPERTY;
        }
    }

    void
    GetAllProps(
        const InterfaceDescription::Member* member,
        Message&                            msg
        )
    {
        cout << "Received alljoyn GetAllProps request for " <<
                member->iface->GetName() << ":" << member->name << endl;

        if(!bus)
        {
            return;
        }

        pthread_mutex_lock(&m_ReplyReceivedMutex);
        m_ReplyReceived = false;
        m_ReplyStr = "";

        ((AllJoynHandler*)((GenericBusAttachment*)bus)->GetBusListener())->
                HandleAllJoynGetAllRequest(member,
                ((GenericBusAttachment*)bus)->GetAppName(), this->GetPath());

        // Wait for the XMPP response signal
        struct timespec wait_time;
        wait_time.tv_sec = time(NULL)+10; wait_time.tv_nsec = 0;
        while(!m_ReplyReceived)
        {
            if(ETIMEDOUT == pthread_cond_timedwait(
                    &m_ReplyReceivedWaitCond,
                    &m_ReplyReceivedMutex,
                    &wait_time))
            {
                break;
            }
        }

        bool replyReceived = m_ReplyReceived;
        string replyStr = m_ReplyStr;

        pthread_mutex_unlock(&m_ReplyReceivedMutex);

        if(replyReceived)
        {
            MsgArg result = msgarg::FromString(replyStr);
            QStatus err = MethodReply(msg, &result, 1);
            if(err != ER_OK)
            {
                cout << "Failed to send method reply to GetAllProps request: "
                        << QCC_StatusText(err) << endl;
            }
        }
    }

    QStatus
    Set(
        const char* ifcName,
        const char* propName,
        MsgArg&     val
        )
    {
        cout << "\n\nSET REQUEST!!\n\n" << endl;
        return ER_BUS_NO_SUCH_PROPERTY;
    }

    /*void GetAllProps(const InterfaceDescription::Member* member, Message& msg)
    {
        cout << "\n\nGET ALL PROPERTIES!!!\n\n" << endl;
    }*/

    void
    SignalReplyReceived(
        string replyStr
        )
    {
        pthread_mutex_lock(&m_ReplyReceivedMutex);
        m_ReplyReceived = true;
        m_ReplyStr = replyStr;
        pthread_cond_signal(&m_ReplyReceivedWaitCond);
        pthread_mutex_unlock(&m_ReplyReceivedMutex);
    }

    bool
    HasInterface(
        string ifaceName,
        string memberName = ""
        )
    {
        vector<const InterfaceDescription*>::iterator if_it;
        for(if_it = m_Interfaces.begin(); if_it != m_Interfaces.end(); ++if_it)
        {
            if(ifaceName == (*if_it)->GetName())
            {
                if(memberName.empty())
                {
                    return true;
                }

                size_t numMembers = (*if_it)->GetMembers();
                InterfaceDescription::Member** members =
                        new InterfaceDescription::Member*[numMembers];
                numMembers = (*if_it)->GetMembers(
                        (const InterfaceDescription::Member**)members,
                        numMembers);
                for(uint32_t i = 0; i < numMembers; ++i)
                {
                    if(memberName == members[i]->name.c_str())
                    {
                        return true;
                    }
                }

                delete[] members;
            }
        }

        return false;
    }

    void
    SendSignal(
        string    destination,
        SessionId sid,
        string    ifaceName,
        string    memberName,
        MsgArg*   margs,
        size_t    numArgs
        )
    {
        // Get the InterfaceDescription::Member
        vector<const InterfaceDescription*>::iterator if_it;
        for(if_it = m_Interfaces.begin(); if_it != m_Interfaces.end(); ++if_it)
        {
            if(ifaceName == (*if_it)->GetName())
            {
                size_t numMembers = (*if_it)->GetMembers();
                InterfaceDescription::Member** members =
                        new InterfaceDescription::Member*[numMembers];
                numMembers = (*if_it)->GetMembers(
                        (const InterfaceDescription::Member**)members,
                        numMembers);
                for(uint32_t i = 0; i < numMembers; ++i)
                {
                    if(memberName == members[i]->name.c_str())
                    {
                        QStatus err = Signal(destination.c_str(), sid,
                                *members[i], margs, numArgs);
                        if(err != ER_OK)
                        {
                            cout << "Failed to send signal: " <<
                                    QCC_StatusText(err) << endl;
                        }
                    }
                }

                delete[] members;
            }
        }
    }

private:
    bool m_ReplyReceived;
    string m_ReplyStr;
    pthread_mutex_t m_ReplyReceivedMutex;
    pthread_cond_t m_ReplyReceivedWaitCond;

    vector<const InterfaceDescription*> m_Interfaces;
};

class GenericPropertyStore :
    public PropertyStore
{
public:
    GenericPropertyStore(
        MsgArg& allProperties
        ) :
        m_AllProperties(allProperties)
    {}

    ~GenericPropertyStore()
    {}

    QStatus
    ReadAll(
        const char* languageTag,
        Filter      filter,
        MsgArg&     all
        )
    {
        cout << "ReadAll called: " << filter << endl;

        all = m_AllProperties;
        return ER_OK;
    }

    QStatus
    Update(
        const char*   name,
        const char*   languageTag,
        const MsgArg* value
        )
    {
        cout << "UPDATE CALLED" << endl;
        return ER_NOT_IMPLEMENTED;
    }

    QStatus
    Delete(
        const char* name,
        const char* languageTag
        )
    {
        cout << "DELETE CALLED" << endl;
        return ER_NOT_IMPLEMENTED;
    }

private:
    MsgArg m_AllProperties;
};

// BusObject we attach to a BusAttachment when we need to relay an announcement
class AboutBusObject :
    public AboutService
{
public:
    AboutBusObject(
        BusAttachment& bus,
        PropertyStore* store
        ) :
        AboutService(bus, *store),
        m_PropertyStore(0)
    {}

    ~AboutBusObject()
    {
        delete m_PropertyStore;
    }

    QStatus
    AddObjectDescriptionsFromXmppInfo(
        string info
        )
    {
        istringstream info_stream(info);
        string line, version, port_str, busName;

        // First line is the type (announcement)
        if(0 == getline(info_stream, line)){ return ER_FAIL; }
        if(line != ALLJOYN_CODE_ANNOUNCE){ return ER_FAIL; }

        // Get the info from the message
        if(0 == getline(info_stream, version)){ return ER_FAIL; }
        if(0 == getline(info_stream, port_str)){ return ER_FAIL; }
        if(0 == getline(info_stream, busName)){ return ER_FAIL; }

        // The object descriptions follow
        string objectPath = "";
        vector<String> interfaceNames;
        while(0 != getline(info_stream, line))
        {
            if(line.empty())
            {
                break;
            }

            if(objectPath.empty())
            {
                objectPath = line;
            }
            else
            {
                if(line[0] == '/')
                {
                    // end of the object description
                    this->AddObjectDescription(
                            objectPath.c_str(), interfaceNames);
                    objectPath = line;
                    interfaceNames.clear();
                }
                else
                {
                    interfaceNames.push_back(line.c_str());
                }
            }
        }

        return ER_OK;
    }

    static
    PropertyStore*
    CreatePropertyStoreFromXmppInfo(
        string info
        )
    {
        str::UnescapeXml(info);
        istringstream info_stream(info);
        string line, version, port_str, busName;

        // First line is the type (announcement)
        if(0 == getline(info_stream, line)){ return NULL; }
        if(line != ALLJOYN_CODE_ANNOUNCE){ return NULL; }

        // Get the info from the message
        if(0 == getline(info_stream, version)){ return NULL; }
        if(0 == getline(info_stream, port_str)){ return NULL; }
        if(0 == getline(info_stream, busName)){ return NULL; }

        // The object descriptions follow
        string objectPath = "";
        vector<String> interfaceNames;
        while(0 != getline(info_stream, line))
        {
            if(line.empty())
            {
                break;
            }
        }

        // Then come the properties
        vector<MsgArg> dictArgs;
        string propName = "", propDesc = "";
        while(0 != getline(info_stream, line))
        {
            if(line.empty())
            {
                // reached the end of a property
                string key = propName;
                MsgArg val = msgarg::FromString(propDesc);
                MsgArg dictEntry("{sv}", key.c_str(), &val);
                dictEntry.Stabilize();

                dictArgs.push_back(dictEntry);

                propName.clear();
                propDesc.clear();
            }

            if(propName.empty())
            {
                propName = line;
            }
            else
            {
                propDesc += line;
            }
        }

        MsgArg args("a{sv}", dictArgs.size(), &dictArgs[0]);
        args.Stabilize();

        return new GenericPropertyStore(args);
    }

    QStatus
    Get(
        const char* ifcName,
        const char* propName,
        MsgArg&     val
        )
    {
        cout << "\n\nGET REQUEST!!\n\n" << endl;
        return ER_BUS_NO_SUCH_PROPERTY;
    }

    QStatus
    Set(
        const char* ifcName,
        const char* propName,
        MsgArg&     val
        )
    {
        cout << "\n\nSET REQUEST!!\n\n" << endl;
        return ER_BUS_NO_SUCH_PROPERTY;
    }

private:
    PropertyStore* m_PropertyStore;
};

XMPPConnector::XMPPConnector(
    BusAttachment* bus,
    string         appName,
    string         jabberId,
    string         password,
    string         chatroomJabberId
    ) :
#ifndef NO_AJ_GATEWAY
    GatewayConnector(bus, appName.c_str()),
#endif // !NO_AJ_GATEWAY
    m_Bus(bus),
    m_BusAttachments(),
    m_UnsentAnnouncements(),
    m_JabberId(jabberId),
    m_Password(password),
    m_ChatroomJabberId(chatroomJabberId),
    m_AboutPropertyStore(NULL),
    m_NotifService(NULL),
    m_NotifSender(NULL)
{
    // Initialize our XMPP connection
    xmpp_initialize();
    m_XmppCtx = xmpp_ctx_new(NULL, NULL);
    m_XmppConn = xmpp_conn_new(m_XmppCtx);

    m_BusListener = new AllJoynHandler(this, m_XmppConn);
    m_Bus->RegisterBusListener(*m_BusListener);

    // TODO:
    String deviceid;
    GuidUtil::GetInstance()->GetDeviceIdString(&deviceid);
    String appid;
    GuidUtil::GetInstance()->GenerateGUID(&appid);
    size_t len = appid.length()/2;
    uint8_t* bytes = new uint8_t[len];
    HexStringToBytes(appid, bytes, len);
    MsgArg appidArg("ay", len, bytes);
    appidArg.Stabilize();
    MsgArg args = msgarg::FromString(
            "<array type_sig=\"{sv}\">\
              <dict_entry>\
                <string>AppId</string>\
                <variant signature=\"ay\">"
                + msgarg::ToString(appidArg) +
                "</variant>\
              </dict_entry>\
              <dict_entry>\
                <string>AppName</string>\
                <variant signature=\"s\">\
                  <string>Notifier</string>\
                </variant>\
              </dict_entry>\
              <dict_entry>\
                <string>DefaultLanguage</string>\
                <variant signature=\"s\">\
                  <string>en</string>\
                </variant>\
              </dict_entry>\
              <dict_entry>\
                <string>DeviceId</string>\
                <variant signature=\"s\">\
                  <string>"+string(deviceid.c_str())+"</string>\
                </variant>\
              </dict_entry>\
              <dict_entry>\
                <string>DeviceName</string>\
                <variant signature=\"s\">\
                  <string>Alarm Service</string>\
                </variant>\
              </dict_entry>\
              <dict_entry>\
                <string>Manufacturer</string>\
                <variant signature=\"s\">\
                  <string>Affinegy</string>\
                </variant>\
              </dict_entry>\
              <dict_entry>\
                <string>ModelNumber</string>\
                <variant signature=\"s\">\
                  <string>1.0</string>\
                </variant>\
              </dict_entry>\
            </array>");

    m_AboutPropertyStore = new GenericPropertyStore(args);
    AboutServiceApi::Init(*m_Bus, *m_AboutPropertyStore);
    AboutServiceApi* aboutService = AboutServiceApi::getInstance();
    SessionPort sp = 900;
    SessionOpts opts(SessionOpts::TRAFFIC_MESSAGES, false,
            SessionOpts::PROXIMITY_ANY, TRANSPORT_ANY);
    m_Bus->BindSessionPort(sp, opts, *(AllJoynHandler*)m_BusListener);
    aboutService->Register(sp);
    m_Bus->RegisterBusObject(*aboutService);
    aboutService->Announce();

    m_NotifService = NotificationService::getInstance();
    m_NotifSender = m_NotifService->initSend(m_Bus, m_AboutPropertyStore);

    // Well-known ports that we need to bind (temporary)
    m_SessionPorts.push_back(27); // org.alljoyn.bus.samples.chat
    m_SessionPorts.push_back(1000); // ControlPanel
}

XMPPConnector::~XMPPConnector()
{
    m_Bus->UnregisterBusListener(*m_BusListener);
    delete m_BusListener;

    xmpp_conn_release(m_XmppConn);
    xmpp_ctx_free(m_XmppCtx);
    xmpp_shutdown();

    vector<BusAttachment*>::iterator it;
    for(it = m_BusAttachments.begin(); it != m_BusAttachments.end(); ++it)
    {
        (*it)->Disconnect();
        (*it)->Stop();
        delete(*it);
    }
}

QStatus
XMPPConnector::Start()
{
    // Set up our xmpp connection
    xmpp_conn_set_jid(m_XmppConn, m_JabberId.c_str());
    xmpp_conn_set_pass(m_XmppConn, m_Password.c_str());
    xmpp_handler_add(
            m_XmppConn, XmppStanzaHandler, NULL, "message", NULL, this);
    if(0 != xmpp_connect_client(
            m_XmppConn, NULL, 0, XmppConnectionHandler, this))
    {
        cout << "Failed to connect to XMPP server." << endl;
        return ER_FAIL;
    }

    // Listen for XMPP
    xmpp_run(m_XmppCtx);

    return ER_OK;
}

void XMPPConnector::Stop()
{
    xmpp_disconnect(m_XmppConn);
    xmpp_handler_delete(m_XmppConn, XmppStanzaHandler);
}

QStatus
XMPPConnector::AddRemoteInterface(
    string                  name,
    vector<RemoteBusObject> busObjects,
    bool                    advertise,
    BusAttachment**         bus
    )
{
    QStatus err = ER_OK;
    GenericBusAttachment* new_attachment =
            new GenericBusAttachment(name.c_str());
    AllJoynHandler* ajHandler = new AllJoynHandler(this, m_XmppConn);
    new_attachment->AddBusListener(ajHandler);
    ajHandler->AssociateBusAttachment(new_attachment);

    // Add each bus object and its interfaces
    vector<RemoteBusObject>::iterator obj_it;
    for(obj_it = busObjects.begin(); obj_it != busObjects.end(); ++obj_it)
    {
        GenericBusObject* new_object =
                new GenericBusObject(obj_it->objectPath.c_str());
        err = new_object->ImplementInterfaces(
                obj_it->interfaces, new_attachment);
        if(err != ER_OK)
        {
            cout << "Could not add interfaces to bus object: " <<
                    QCC_StatusText(err) << endl;
            delete new_attachment;
            return err;
        }

        err = new_attachment->AddBusObject(new_object);
        if(err != ER_OK)
        {
            cout << "Could not register bus object: " <<
                    QCC_StatusText(err) << endl;
            delete new_attachment;
            return err;
        }
    }

    // Start and connect the new bus attachment
    err = new_attachment->Start();
    if(err != ER_OK)
    {
        cout << "Could not start new bus attachment: " <<
                QCC_StatusText(err) << endl;
        delete new_attachment;
        return err;
    }
    err = new_attachment->Connect();
    if(err != ER_OK)
    {
        cout << "Could not connect new bus attachment: " <<
                QCC_StatusText(err) << endl;
        new_attachment->Stop();
        delete new_attachment;
        return err;
    }


    // Listen for session requests
    vector<SessionPort>::iterator port_iter;
    for(port_iter = m_SessionPorts.begin();
        port_iter != m_SessionPorts.end();
        ++port_iter)
    {
        SessionOpts opts(SessionOpts::TRAFFIC_MESSAGES, true,
                SessionOpts::PROXIMITY_ANY, TRANSPORT_ANY);
        QStatus err = new_attachment->BindSessionPort(
                *port_iter, opts, *ajHandler); // TODO: traffic messages only?
        if(err != ER_OK)
        {
            cout << "Failed to bind AllJoyn session port " <<
                    *port_iter << ": " << QCC_StatusText(err) << endl;
            new_attachment->Disconnect();
            new_attachment->Stop();
            delete new_attachment;
            return err;
        }
    }

    m_BusAttachments.push_back(new_attachment);
    if(advertise)
    {
        if(name.find_first_of(":") != 0)
        {
            // Request and advertise the new attachment's name
            err = new_attachment->RequestName(name.c_str(),
                    DBUS_NAME_FLAG_ALLOW_REPLACEMENT|DBUS_NAME_FLAG_DO_NOT_QUEUE
                    );
            if(err != ER_OK)
            {
                cout << "Could not acquire well known name " << name << ": " <<
                        QCC_StatusText(err) << endl;
                m_BusAttachments.pop_back();
                new_attachment->Disconnect();
                new_attachment->Stop();
                delete new_attachment;
                return err;
            }
        }
        else
        {
            // We have a device advertising its unique name.
            name = new_attachment->GetUniqueName().c_str();
            new_attachment->SetAdvertisedName(name);
        }

        cout << "advertising name: " << name << endl;
        err = new_attachment->AdvertiseName(name.c_str(), TRANSPORT_ANY);
        if(err != ER_OK)
        {
            cout << "Could not advertise name " << name << ": " <<
                    QCC_StatusText(err) << endl;
            m_BusAttachments.pop_back();
            new_attachment->Disconnect();
            new_attachment->Stop();
            delete new_attachment;
            return err;
        }
    }

    if(bus)
    {
        *bus = new_attachment;
    }
    return err;
}

string
XMPPConnector::FindWellKnownName(
    string uniqueName
    )
{
    vector<BusAttachment*>::iterator it;
    for(it = m_BusAttachments.begin(); it != m_BusAttachments.end(); ++it)
    {
        if(uniqueName == (*it)->GetUniqueName().c_str())
        {
            return ((GenericBusAttachment*)(*it))->GetAdvertisedName();
        }
    }

    return "";
}

BusAttachment*
XMPPConnector::GetBusAttachment()
{
    return m_Bus;
}

BusListener*
XMPPConnector::GetBusListener()
{
    return m_BusListener;
}

string
XMPPConnector::GetJabberId()
{
    return m_JabberId;
}

string
XMPPConnector::GetPassword()
{
    return m_Password;
}

string XMPPConnector::GetChatroomJabberId()
{
    return m_ChatroomJabberId;
}

bool
XMPPConnector::IsAdvertisingName(
    string name
    )
{
    vector<BusAttachment*>::iterator it;
    for(it = m_BusAttachments.begin(); it != m_BusAttachments.end(); ++it)
    {
        GenericBusAttachment* gen_bus = (GenericBusAttachment*)(*it);
        if(name == gen_bus->GetAdvertisedName())
        {
            return true;
        }
    }
    return false;
}

#ifndef NO_AJ_GATEWAY
void
XMPPConnector::mergedAclUpdated()
{
    // TODO
}

void XMPPConnector::shutdown()
{
    // TODO
}

void
XMPPConnector::receiveGetMergedAclAsync(
    QStatus           unmarshalStatus,
    GatewayMergedAcl* response
    )
{
    // TODO
}
#endif // !NO_AJ_GATEWAY

void
XMPPConnector::RelayAnnouncement(
    BusAttachment* bus,
    string info
    )
{
    GenericBusAttachment* gen_bus = (GenericBusAttachment*)bus;
    cout << "Relaying announcement for " <<
            gen_bus->GetAdvertisedName() << endl;

    PropertyStore* properties =
            AboutBusObject::CreatePropertyStoreFromXmppInfo(info);
    AboutBusObject* about_obj = new AboutBusObject(*gen_bus, properties);
    about_obj->AddObjectDescriptionsFromXmppInfo(info);

    // Bind the announced session port
    SessionOpts opts(SessionOpts::TRAFFIC_MESSAGES, true,
            SessionOpts::PROXIMITY_ANY, TRANSPORT_ANY);
    SessionPort sp = 900; // TODO: get real port
    gen_bus->BindSessionPort(
            sp, opts, *(AllJoynHandler*)(gen_bus->GetBusListener()));

    about_obj->Register(900); // TODO: get real port
    gen_bus->AddBusObject(about_obj);

    QStatus err = about_obj->Announce();
    if(err != ER_OK)
    {
        cout << "Failed to relay announcement: " << QCC_StatusText(err) << endl;
    }
}

void
XMPPConnector::HandleIncomingAdvertisement(
    string info
    )
{
    // Parse the required information
    string advertise_name;
    vector<RemoteBusObject> bus_objects;

    istringstream info_stream(info);
    string line;

    // First line is the type (advertisement)
    if(0 == getline(info_stream, line)){ return; }
    if(line != ALLJOYN_CODE_ADVERTISEMENT){ return; }

    // Second line is the name to advertise
    if(0 == getline(info_stream, advertise_name)){ return; }
    //cout << "advertise name: " << advertise_name << endl;

    // Following are the bus objects and their interfaces separated by newlines // TODO: copy-paste code
    RemoteBusObject new_bus_object;
    string interface_name = "";
    string interface_description = "";

    if(0 == getline(info_stream, line)){ return; }
    while(getline(info_stream, line))
    {
        if(line.empty())
        {
            if(!interface_description.empty())
            {
                // We've reached the end of an interface description.
                str::UnescapeXml(interface_description);
                QStatus err = m_Bus->CreateInterfacesFromXml(
                        interface_description.c_str());
                if(err == ER_OK)
                {
                    const InterfaceDescription* new_interface =
                            m_Bus->GetInterface(interface_name.c_str());
                    if(new_interface)
                    {
                        new_bus_object.interfaces.push_back(new_interface);
                    }
                }
                else
                {
                    cout << "Failed to create InterfaceDescription " <<
                            interface_name << ": " <<
                            QCC_StatusText(err) << endl;
                }

                interface_name.clear();
                interface_description.clear();
            }
            else
            {
                // We've reached the end of a bus object.
                bus_objects.push_back(new_bus_object);

                new_bus_object.objectPath.clear();
                new_bus_object.interfaces.clear();
            }
        }
        else
        {
            if(new_bus_object.objectPath.empty())
            {
                new_bus_object.objectPath = line;
            }
            else if(interface_name.empty())
            {
                interface_name = line;
            }
            else
            {
                interface_description.append(line + "/n");
            }
        }
    }

    // Now we need to actually implement the interfaces and advertise the name
    GenericBusAttachment* gen_bus;
    QStatus err = AddRemoteInterface(advertise_name, bus_objects,
            true, (BusAttachment**)&gen_bus);
    if(err != ER_OK)
    {
        cout << "Could not implement remote advertisement." << endl;
    }
    else
    {
        // Is there an announcement waiting to be sent for this new attachment?
        if(m_UnsentAnnouncements.find(advertise_name) !=
                m_UnsentAnnouncements.end())
        {
            RelayAnnouncement(
                    gen_bus, m_UnsentAnnouncements.at(advertise_name));
            m_UnsentAnnouncements.erase(advertise_name);
        }
    }
}

void
XMPPConnector::HandleIncomingMethodCall(
    string info
    )
{
    // Parse the required information
    istringstream info_stream(info);
    string line, appName, destName, destPath,
            ifaceName, memberName, remoteSessionId;

    // First line is the type (method call)
    if(0 == getline(info_stream, line)){ return; }
    if(line != ALLJOYN_CODE_METHOD_CALL){ return; }

    if(0 == getline(info_stream, appName)){ return; }
    if(0 == getline(info_stream, destName)){ return; }
    if(0 == getline(info_stream, destPath)){ return; }
    if(0 == getline(info_stream, ifaceName)){ return; }
    if(0 == getline(info_stream, memberName)){ return; }
    if(0 == getline(info_stream, remoteSessionId)){ return; }

    // The rest is the message arguments
    string messageArgsString = "";
    while(0 != getline(info_stream, line))
    {
        messageArgsString += line + "\n";
    }

    // Find the bus attachment with this busName
    GenericBusAttachment* found_bus = 0;

    vector<BusAttachment*>::iterator it;
    for(it = m_BusAttachments.begin(); it != m_BusAttachments.end(); ++it)
    {
        GenericBusAttachment* gen_bus = (GenericBusAttachment*)(*it);
        cout << gen_bus->GetAppName() << endl;
        if(gen_bus->GetAppName() == appName)
        {
            found_bus = gen_bus;
            break;
        }
    }

    if(!found_bus)
    {
        cout << "No bus attachment to handle incoming method call." << endl;
        return;
    }

    // Call the method
    SessionId localSid = found_bus->GetLocalSessionId(
            strtol(remoteSessionId.c_str(), NULL, 10));
    ProxyBusObject proxy(
            *found_bus, destName.c_str(), destPath.c_str(), localSid);
    QStatus err = proxy.IntrospectRemoteObject();
    if(err != ER_OK)
    {
        cout << "Failed to introspect remote object to relay method call: " <<
                QCC_StatusText(err) << endl;
        return;
    }

    vector<MsgArg> messageArgs = msgarg::VectorFromString(messageArgsString);
    Message reply(*found_bus);
    err = proxy.MethodCall(ifaceName.c_str(), memberName.c_str(),
            &messageArgs[0], messageArgs.size(), reply, 5000);
    if(err != ER_OK)
    {
        cout << "Failed to relay method call: " << QCC_StatusText(err) << endl;
        return;
    }

    // Return the reply
    size_t numReplyArgs;
    const MsgArg* replyArgs = 0;
    reply->GetArgs(numReplyArgs, replyArgs);

    ostringstream msg_stream;
    msg_stream << ALLJOYN_CODE_METHOD_REPLY << "\n";
    msg_stream << destName << "\n";
    msg_stream << destPath << "\n";
    msg_stream << msgarg::ToString(replyArgs, numReplyArgs) << "\n";

    // Now wrap it in an XMPP stanza
    xmpp_stanza_t* message = xmpp_stanza_new(xmpp_conn_get_context(m_XmppConn));

    xmpp_stanza_set_name(message, "message");
    xmpp_stanza_set_attribute(message, "to", m_ChatroomJabberId.c_str());
    xmpp_stanza_set_type(message, "groupchat");

    xmpp_stanza_t* body = xmpp_stanza_new(m_XmppCtx);
    xmpp_stanza_set_name(body, "body");
    xmpp_stanza_t* text = xmpp_stanza_new(m_XmppCtx);
    xmpp_stanza_set_text(text, msg_stream.str().c_str());
    xmpp_stanza_add_child(body, text);
    xmpp_stanza_release(text);
    xmpp_stanza_add_child(message, body);
    xmpp_stanza_release(body);

    char* buf = 0;
    size_t buflen = 0;
    xmpp_stanza_to_text(message, &buf, &buflen);
    cout << "Sending XMPP method reply message" << endl;
    free(buf);

    // Send it back
    xmpp_send(m_XmppConn, message);
    xmpp_stanza_release(message);
}

void
XMPPConnector::HandleIncomingMethodReply(
    string info
    )
{
    // Parse the required information
    istringstream info_stream(info);
    string line, appName, objPath;

    // First line is the type (method reply)
    if(0 == getline(info_stream, line)){ return; }
    if(line != ALLJOYN_CODE_METHOD_REPLY){ return; }

    if(0 == getline(info_stream, appName)){ return; }
    if(0 == getline(info_stream, objPath)){ return; }

    // The rest is the message arguments
    string messageArgsString = "";
    while(0 != getline(info_stream, line))
    {
        messageArgsString += line + "\n";
    }

    // Find the bus attachment with this busName
    GenericBusAttachment* found_bus = 0;

    vector<BusAttachment*>::iterator it;
    for(it = m_BusAttachments.begin(); it != m_BusAttachments.end(); ++it)
    {
        GenericBusAttachment* gen_bus = (GenericBusAttachment*)(*it);
        cout << gen_bus->GetAppName() << endl;
        if(gen_bus->GetAppName() == appName)
        {
            found_bus = gen_bus;
            break;
        }
    }

    if(!found_bus)
    {
        cout << "No bus attachment to handle incoming method call." << endl;
        return;
    }

    // Tell the attachment we received a message reply
    str::UnescapeXml(messageArgsString);
    GenericBusObject* genObj =
            (GenericBusObject*)found_bus->GetBusObject(objPath);
    genObj->SignalReplyReceived(messageArgsString);
}

void
XMPPConnector::HandleIncomingSignal(
    string info
    )
{
    // Parse the required information
    string advertise_name;
    vector<RemoteBusObject> bus_objects;

    istringstream info_stream(info);
    string line, appName, destination, remoteId, ifaceName, ifaceMember;

    // First line is the type (signal)
    if(0 == getline(info_stream, line)){ return; }
    if(line != ALLJOYN_CODE_SIGNAL){ return; }

    // Get the bus name and remote session ID
    if(0 == getline(info_stream, appName)){ return; }
    if(0 == getline(info_stream, destination)){ return; }
    if(0 == getline(info_stream, remoteId)){ return; }
    if(0 == getline(info_stream, ifaceName)){ return; }
    if(0 == getline(info_stream, ifaceMember)){ return; }

    // The rest is the message arguments
    string messageArgsString = "";
    while(0 != getline(info_stream, line))
    {
        messageArgsString += line + "\n";
    }

    // Find the bus attachment with this busName
    GenericBusAttachment* found_bus = 0;

    vector<BusAttachment*>::iterator it;
    for(it = m_BusAttachments.begin(); it != m_BusAttachments.end(); ++it)
    {
        GenericBusAttachment* gen_bus = (GenericBusAttachment*)(*it);
        if(gen_bus->GetAppName() == appName)
        {
            found_bus = gen_bus;
            break;
        }
    }

    if(!found_bus)
    {
        cout << "No bus attachment to handle incoming signal." << endl;
        return;
    }

    // Now find the bus object and interface member of the signal
    GenericBusObject* bus_obj = 0;
    vector<BusObject*> busObjs = found_bus->GetBusObjects();

    vector<BusObject*>::iterator obj_it;
    for(obj_it = busObjs.begin(); obj_it != busObjs.end(); ++obj_it)
    {
        if(((GenericBusObject*)(*obj_it))->HasInterface(ifaceName, ifaceMember))
        {
            bus_obj = (GenericBusObject*)*obj_it;
            break;
        }
    }

    if(bus_obj)
    {
        SessionId sid = found_bus->GetLocalSessionId(
                strtol(remoteId.c_str(), NULL, 10));
        str::UnescapeXml(messageArgsString);
        vector<MsgArg> margs = msgarg::VectorFromString(messageArgsString);
        bus_obj->SendSignal(destination, sid, ifaceName, ifaceMember,
                &margs[0], margs.size());
    }
    else
    {
        cout << "Could not find bus object to relay signal" << endl;
    }
}

void
XMPPConnector::HandleIncomingJoinRequest(
    string info
    )
{
    string join_dest = "";
    string port_str = "";
    string joiner = "";

    istringstream info_stream(info);
    string line;

    // First line is the type (join request)
    if(0 == getline(info_stream, line)){ return; }
    if(line != ALLJOYN_CODE_JOIN_REQUEST){ return; }

    // Next is the session port, join destination, and the joiner
    if(0 == getline(info_stream, join_dest)){ return; }
    if(0 == getline(info_stream, port_str)){ return; }
    if(0 == getline(info_stream, joiner)){ return; }

    // Then follow the interfaces implemented by the joiner                     // TODO: copy-paste code
    vector<RemoteBusObject> bus_objects;
    RemoteBusObject new_bus_object;
    string interface_name = "";
    string interface_description = "";

    while(getline(info_stream, line))
    {
        if(line.empty())
        {
            if(!interface_description.empty())
            {
                // We've reached the end of an interface description.
                str::UnescapeXml(interface_description);
                QStatus err = m_Bus->CreateInterfacesFromXml(
                        interface_description.c_str());
                if(err == ER_OK)
                {
                    const InterfaceDescription* new_interface =
                            m_Bus->GetInterface(interface_name.c_str());
                    if(new_interface)
                    {
                        new_bus_object.interfaces.push_back(new_interface);
                    }
                }
                else
                {
                    cout << "Failed to create InterfaceDescription " <<
                            interface_name << ": " <<
                            QCC_StatusText(err) << endl;
                }

                interface_name.clear();
                interface_description.clear();
            }
            else
            {
                // We've reached the end of a bus object.
                bus_objects.push_back(new_bus_object);

                new_bus_object.objectPath.clear();
                new_bus_object.interfaces.clear();
            }
        }
        else
        {
            if(new_bus_object.objectPath.empty())
            {
                new_bus_object.objectPath = line;
            }
            else if(interface_name.empty())
            {
                interface_name = line;
            }
            else
            {
                interface_description.append(line + "/n");
            }
        }
    }

    // Create a bus attachment to start a session with                          // TODO: look for existing one first
    GenericBusAttachment* new_bus = 0;
    SessionId id = 0;
    QStatus err = AddRemoteInterface(
            joiner, bus_objects, false, (BusAttachment**)&new_bus);
    if(err == ER_OK && new_bus)
    {
        // Try to join a session of our own
        SessionPort port = strtol(port_str.c_str(), NULL, 10);
        SessionOpts opts(SessionOpts::TRAFFIC_MESSAGES, true,
                SessionOpts::PROXIMITY_ANY, TRANSPORT_ANY);
        err = new_bus->JoinSession(join_dest.c_str(), port, NULL, id, opts);

        if(err == ER_OK)
        {
            // Register signal handlers for the interfaces we're joining with   // TODO: this info could be sent via XMPP from the connector joinee instead of introspected again
            vector<XMPPConnector::RemoteBusObject> joiner_objects;
            ProxyBusObject proxy(*new_bus, join_dest.c_str(), "/", id);
            AllJoynHandler::GetInterfacesRecursive(joiner_objects, proxy);

            vector<XMPPConnector::RemoteBusObject>::const_iterator it;
            for(it = joiner_objects.begin(); it != joiner_objects.end(); ++it)
            {
                vector<const InterfaceDescription*>::const_iterator if_it;
                for(if_it = it->interfaces.begin();
                    if_it != it->interfaces.end();
                    ++if_it)
                {
                    string interfaceName = (*if_it)->GetName();

                    // Register signal listeners here.                          // TODO: sessionless signals? register on advertise/announce
                    size_t num_members = (*if_it)->GetMembers();
                    InterfaceDescription::Member** interface_members =
                            new InterfaceDescription::Member*[num_members];
                    num_members = (*if_it)->GetMembers(
                            (const InterfaceDescription::Member**)
                            interface_members, num_members);
                    for(uint32_t i = 0; i < num_members; ++i)
                    {
                        if(interface_members[i]->memberType == MESSAGE_SIGNAL)
                        {
                            QStatus err = new_bus->RegisterSignalHandler(
                                    (AllJoynHandler*)m_BusListener,
                                    static_cast<MessageReceiver::SignalHandler>(
                                    &AllJoynHandler::AllJoynSignalHandler),
                                    interface_members[i], NULL);
                            if(err != ER_OK)
                            {
                                cout << "Could not register signal handler for "
                                        << interfaceName << ":" <<
                                        interface_members[i]->name << endl;
                            }
                            else
                            {
                                cout << "Registered signal handler for " <<
                                        interfaceName << ":" <<
                                        interface_members[i]->name << endl;
                            }
                        }
                    }
                    delete[] interface_members;
                }
            }
        }
    }
    else if(err == ER_OK)
    {
        err = ER_FAIL;
    }

    // Send the status back to the original session joiner
    ostringstream msg_stream;
    msg_stream << ALLJOYN_CODE_JOIN_RESPONSE << "\n";                           // TODO: support more than 1 concurrent session request
    msg_stream << join_dest << "\n";
    msg_stream << (err == ER_OK ? id : 0) << "\n";


    // Now wrap it in an XMPP stanza
    xmpp_stanza_t* message = xmpp_stanza_new(xmpp_conn_get_context(m_XmppConn));

    xmpp_stanza_set_name(message, "message");
    xmpp_stanza_set_attribute(message, "to", m_ChatroomJabberId.c_str());
    xmpp_stanza_set_type(message, "groupchat");

    xmpp_stanza_t* body = xmpp_stanza_new(m_XmppCtx);
    xmpp_stanza_set_name(body, "body");
    xmpp_stanza_t* text = xmpp_stanza_new(m_XmppCtx);
    xmpp_stanza_set_text(text, msg_stream.str().c_str());
    xmpp_stanza_add_child(body, text);
    xmpp_stanza_release(text);
    xmpp_stanza_add_child(message, body);
    xmpp_stanza_release(body);

    char* buf = 0;
    size_t buflen = 0;
    xmpp_stanza_to_text(message, &buf, &buflen);
    cout << "Sending XMPP join response message" << endl;
    free(buf);

    // Send it back
    xmpp_send(m_XmppConn, message);
    xmpp_stanza_release(message);
}

void
XMPPConnector::HandleIncomingJoinResponse(
    string info
    )
{
    istringstream info_stream(info);
    string line;
    string appName = "";
    string result_str = "";

    // First line is the type (join response)
    if(0 == getline(info_stream, line)){ return; }
    if(line != ALLJOYN_CODE_JOIN_RESPONSE){ return; }

    // Get the info from the message
    if(0 == getline(info_stream, appName)){ return; }
    if(0 == getline(info_stream, result_str)){ return; }

    // Find the BusAttachment with the given app name
    vector<BusAttachment*>::iterator it;
    for(it = m_BusAttachments.begin(); it != m_BusAttachments.end(); ++it)
    {
        GenericBusAttachment* gen_bus = (GenericBusAttachment*)(*it);
        if(gen_bus->GetAppName() == appName)
        {
            AllJoynHandler* ajHandler =
                    (AllJoynHandler*)gen_bus->GetBusListener();
            ajHandler->SignalSessionJoined(
                    strtol(result_str.c_str(), NULL, 10));
            break;
        }
    }
}

void
XMPPConnector::HandleIncomingSessionJoined(
    string info
    )
{
    istringstream info_stream(info);
    string line, joiner, remote_id_str, local_id_str;

    // First line is the type (session joined)
    if(0 == getline(info_stream, line)){ return; }
    if(line != ALLJOYN_CODE_SESSION_JOINED){ return; }

    // Get the info from the message
    if(0 == getline(info_stream, joiner)){ return; }
    if(0 == getline(info_stream, local_id_str)){ return; }
    if(0 == getline(info_stream, remote_id_str)){ return; }

    if(local_id_str.empty() || remote_id_str.empty())
    {
        return;
    }

    // Find the BusAttachment with the given app name
    vector<BusAttachment*>::iterator it;
    for(it = m_BusAttachments.begin(); it != m_BusAttachments.end(); ++it)
    {
        GenericBusAttachment* gen_bus = (GenericBusAttachment*)(*it);
        if(gen_bus->GetAppName() == joiner)
        {
            // Add the pair of session IDs
            gen_bus->AddSessionIdPair(strtol(remote_id_str.c_str(), NULL, 10),
                    strtol(local_id_str.c_str(), NULL, 10));
        }
    }
}

void
XMPPConnector::HandleIncomingAnnounce(
    string info
    )                                                                           // TODO: Create attachment on announce if !exists. same with foundadvertisedname
{
    istringstream info_stream(info);
    string line, version, port_str, busName;

    // First line is the type (announcement)
    if(0 == getline(info_stream, line)){ return; }
    if(line != ALLJOYN_CODE_ANNOUNCE){ return; }

    // Get the info from the message
    if(0 == getline(info_stream, version)){ return; }
    if(0 == getline(info_stream, port_str)){ return; }
    if(0 == getline(info_stream, busName)){ return; }

    // Find the BusAttachment with the given app name
    vector<BusAttachment*>::iterator it;
    for(it = m_BusAttachments.begin(); it != m_BusAttachments.end(); ++it)
    {
        GenericBusAttachment* gen_bus = (GenericBusAttachment*)(*it);
        if(gen_bus->GetAppName() == busName)
        {
            RelayAnnouncement(gen_bus, info);
            return;
        }
    }

    // No bus attachment with the given name, save this announcement for later
    m_UnsentAnnouncements[busName] = info;
}

void
XMPPConnector::HandleIncomingGetRequest(
    string info
    )
{
    // Parse the required information
    istringstream info_stream(info);
    string line, destName, destPath, ifcName, propName;

    // First line is the type (get request)
    if(0 == getline(info_stream, line)){ return; }
    if(line != ALLJOYN_CODE_GET_PROPERTY){ return; }

    if(0 == getline(info_stream, destName)){ return; }
    if(0 == getline(info_stream, destPath)){ return; }
    if(0 == getline(info_stream, ifcName)){ return; }
    if(0 == getline(info_stream, propName)){ return; }

    // Call the method
    ProxyBusObject proxy(*m_Bus, destName.c_str(), destPath.c_str(), 0);
    QStatus err = proxy.IntrospectRemoteObject();
    if(err != ER_OK)
    {
        cout << "Failed to introspect remote object to relay get request: " <<
                QCC_StatusText(err) << endl;
        return;
    }

    MsgArg value;
    err = proxy.GetProperty(ifcName.c_str(), propName.c_str(), value, 5000);
    if(err != ER_OK)
    {
        cout << "Failed to relay method call: " << QCC_StatusText(err) << endl;
        return;                                                                 // TODO: send the actual response status back
    }

    // Return the reply
    ostringstream msg_stream;
    msg_stream << ALLJOYN_CODE_GET_PROP_REPLY << "\n";
    msg_stream << destName << "\n";
    msg_stream << destPath << "\n";
    msg_stream << msgarg::ToString(value) << "\n";

    // Now wrap it in an XMPP stanza
    xmpp_stanza_t* message = xmpp_stanza_new(xmpp_conn_get_context(m_XmppConn));

    xmpp_stanza_set_name(message, "message");
    xmpp_stanza_set_attribute(message, "to", m_ChatroomJabberId.c_str());
    xmpp_stanza_set_type(message, "groupchat");

    xmpp_stanza_t* body = xmpp_stanza_new(m_XmppCtx);
    xmpp_stanza_set_name(body, "body");
    xmpp_stanza_t* text = xmpp_stanza_new(m_XmppCtx);
    xmpp_stanza_set_text(text, msg_stream.str().c_str());
    xmpp_stanza_add_child(body, text);
    xmpp_stanza_release(text);
    xmpp_stanza_add_child(message, body);
    xmpp_stanza_release(body);

    char* buf = 0;
    size_t buflen = 0;
    xmpp_stanza_to_text(message, &buf, &buflen);
    cout << "Sending XMPP get reply message" << endl;
    free(buf);

    // Send it back
    xmpp_send(m_XmppConn, message);
    xmpp_stanza_release(message);
}

void
XMPPConnector::HandleIncomingGetReply(
    string info
    )
{
    // Parse the required information
    istringstream info_stream(info);
    string line, appName, objPath;

    // First line is the type (get reply)
    if(0 == getline(info_stream, line)){ return; }
    if(line != ALLJOYN_CODE_GET_PROP_REPLY){ return; }

    if(0 == getline(info_stream, appName)){ return; }
    if(0 == getline(info_stream, objPath)){ return; }

    // The rest is the property value
    string messageArgString = "";
    while(0 != getline(info_stream, line))
    {
        messageArgString += line + "\n";
    }

    // Find the bus attachment with this busName
    GenericBusAttachment* found_bus = 0;

    vector<BusAttachment*>::iterator it;
    for(it = m_BusAttachments.begin(); it != m_BusAttachments.end(); ++it)
    {
        GenericBusAttachment* gen_bus = (GenericBusAttachment*)(*it);
        cout << gen_bus->GetAppName() << endl;
        if(gen_bus->GetAppName() == appName)
        {
            found_bus = gen_bus;
            break;
        }
    }

    if(!found_bus)
    {
        cout << "No bus attachment to handle incoming get reply." << endl;
        return;
    }

    // Tell the attachment we received a message reply
    str::UnescapeXml(messageArgString);
    GenericBusObject* genObj =
            (GenericBusObject*)found_bus->GetBusObject(objPath);
    genObj->SignalReplyReceived(messageArgString);
}

void
XMPPConnector::HandleIncomingGetAll(
    string info
    )
{
    // Parse the required information
    istringstream info_stream(info);
    string line, destName, destPath, ifcName, memberName;

    // First line is the type (get request)
    if(0 == getline(info_stream, line)){ return; }
    if(line != ALLJOYN_CODE_GET_ALL){ return; }

    if(0 == getline(info_stream, destName)){ return; }
    if(0 == getline(info_stream, destPath)){ return; }
    if(0 == getline(info_stream, ifcName)){ return; }
    if(0 == getline(info_stream, memberName)){ return; }

    // Call the method
    ProxyBusObject proxy(*m_Bus, destName.c_str(), destPath.c_str(), 0);
    QStatus err = proxy.IntrospectRemoteObject();
    if(err != ER_OK)
    {
        cout << "Failed to introspect remote object to relay get request: " <<
                QCC_StatusText(err) << endl;
        return;
    }

    MsgArg values;
    cout << "Getting all properties..." << endl;
    err = proxy.GetAllProperties(ifcName.c_str(), values, 5000);
    if(err != ER_OK)
    {
        cout << "Failed to get all properties: " << QCC_StatusText(err) << endl;
        return;                                                                 // TODO: send the actual response status back
    }

    // Return the reply
    ostringstream msg_stream;
    msg_stream << ALLJOYN_CODE_GET_ALL_REPLY << "\n";
    msg_stream << destName << "\n";
    msg_stream << destPath << "\n";
    msg_stream << msgarg::ToString(values) << "\n";

    // Now wrap it in an XMPP stanza
    xmpp_stanza_t* message = xmpp_stanza_new(xmpp_conn_get_context(m_XmppConn));

    xmpp_stanza_set_name(message, "message");
    xmpp_stanza_set_attribute(message, "to", m_ChatroomJabberId.c_str());
    xmpp_stanza_set_type(message, "groupchat");

    xmpp_stanza_t* body = xmpp_stanza_new(m_XmppCtx);
    xmpp_stanza_set_name(body, "body");
    xmpp_stanza_t* text = xmpp_stanza_new(m_XmppCtx);
    xmpp_stanza_set_text(text, msg_stream.str().c_str());
    xmpp_stanza_add_child(body, text);
    xmpp_stanza_release(text);
    xmpp_stanza_add_child(message, body);
    xmpp_stanza_release(body);

    char* buf = 0;
    size_t buflen = 0;
    xmpp_stanza_to_text(message, &buf, &buflen);
    cout << "Sending XMPP get reply message" << endl;
    free(buf);

    // Send it back
    xmpp_send(m_XmppConn, message);
    xmpp_stanza_release(message);
}

void
XMPPConnector::HandleIncomingGetAllReply(
    string info
    )
{
    // Parse the required information
    istringstream info_stream(info);
    string line, appName, objPath;

    // First line is the type (get reply)
    if(0 == getline(info_stream, line)){ return; }
    if(line != ALLJOYN_CODE_GET_ALL_REPLY){ return; }

    if(0 == getline(info_stream, appName)){ return; }
    if(0 == getline(info_stream, objPath)){ return; }

    // The rest is the property value
    string messageArgString = "";
    while(0 != getline(info_stream, line))
    {
        messageArgString += line + "\n";
    }

    // Find the bus attachment with this busName
    GenericBusAttachment* found_bus = 0;

    vector<BusAttachment*>::iterator it;
    for(it = m_BusAttachments.begin(); it != m_BusAttachments.end(); ++it)
    {
        GenericBusAttachment* gen_bus = (GenericBusAttachment*)(*it);
        cout << gen_bus->GetAppName() << endl;
        if(gen_bus->GetAppName() == appName)
        {
            found_bus = gen_bus;
            break;
        }
    }

    if(!found_bus)
    {
        cout << "No bus attachment to handle incoming get reply." << endl;
        return;
    }

    // Tell the attachment we received a message reply
    str::UnescapeXml(messageArgString);
    GenericBusObject* genObj =
            (GenericBusObject*)found_bus->GetBusObject(objPath);
    genObj->SignalReplyReceived(messageArgString);
}

void
XMPPConnector::HandleIncomingAlarm(
    string info 
    )
{
    // Parse the message
    istringstream msg_stream(info);
    string token;
    vector<string> lines;
    while (getline(msg_stream, token, '\n'))
    {
        lines.push_back(token);
    }
    map<string,string> alarm_data;
    for ( vector<string>::iterator it(lines.begin());
          lines.end() != it; ++it )
    {
        istringstream line_stream(*it);
        vector<string> tokens;
        while (getline(line_stream, token, ':'))
        {
            tokens.push_back(token);
        }
        // Ignore lines with more than one colon
        if ( tokens.size() == 2 )
        {
            // Add the key/value pair
            alarm_data[tokens.at(0)] = tokens.at(1);
        }
    }

    // Send the Description as an AllJoyn Notification
    if ( alarm_data.end() != alarm_data.find("Description") )
    {
        // TODO:
        cout << "Alarm Description: " << alarm_data.at("Description") << endl;
        NotificationMessageType message_type = INFO;
        NotificationText message("en", alarm_data.at("Description").c_str());
        vector<NotificationText> messages;
        messages.push_back(message);
        Notification notification(message_type, messages);
        QStatus status = m_NotifSender->send(notification, 7200);
        if (status != ER_OK)
        {
            cout << "Failed to send Alarm notification!" << endl;
        }
        else
        {
            cout << "Successfully sent Alarm notification!" << endl;
        }
    }
}

int
XMPPConnector::XmppStanzaHandler(
    xmpp_conn_t* const   conn,
    xmpp_stanza_t* const stanza,
    void* const          userdata
    )
{
    XMPPConnector* xmppConnector = (XMPPConnector*)userdata;

    // Ignore stanzas from ourself
    string from_us = xmppConnector->GetChatroomJabberId()+"/"+
            xmppConnector->GetBusAttachment()->GetGlobalGUIDString().c_str();
    const char* from_attr = xmpp_stanza_get_attribute(stanza, "from");
    if(!from_attr || from_us == from_attr)
    {
        return 1;
    }

    char* buf = 0;
    size_t buflen = 0;
    xmpp_stanza_to_text(stanza, &buf, &buflen);
    free(buf);

    if ( 0 == strcmp("message", xmpp_stanza_get_name(stanza)) )
    {
        xmpp_stanza_t* body = 0;
        char* buf = 0;
        size_t buf_size = 0;
        if(0 != (body = xmpp_stanza_get_child_by_name(stanza, "body")) &&
           0 == (buf_size = xmpp_stanza_to_text(body, &buf, &buf_size)))
        {
            string buf_str(buf);
            xmpp_free(xmpp_conn_get_context(conn), buf);

            // Strip the tags from the message
            if(0 != buf_str.find("<body>") &&
                buf_str.length() != buf_str.find("</body>")+strlen("</body>"))
            {
                // Received an empty message
                return 1;
            }
            buf_str = buf_str.substr(strlen("<body>"),
                    buf_str.length()-strlen("<body></body>"));

            // Handle the content of the message
            string type_code = buf_str.substr(0, buf_str.find_first_of('\n'));
            cout << "Received XMPP message: " << type_code << endl;

            if(type_code == ALLJOYN_CODE_ADVERTISEMENT)
            {
                xmppConnector->HandleIncomingAdvertisement(buf_str);
            }
            else if(type_code == ALLJOYN_CODE_METHOD_CALL)
            {
                xmppConnector->HandleIncomingMethodCall(buf_str);
            }
            else if(type_code == ALLJOYN_CODE_METHOD_REPLY)
            {
                xmppConnector->HandleIncomingMethodReply(buf_str);
            }
            else if(type_code == ALLJOYN_CODE_SIGNAL)
            {
                xmppConnector->HandleIncomingSignal(buf_str);
            }
            /*else if(type_code == ALLJOYN_CODE_NOTIFICATION)
            {
                //TODO: HandleIncomingNotification(buf_str);
            }*/
            else if(type_code == ALLJOYN_CODE_JOIN_REQUEST)
            {
                xmppConnector->HandleIncomingJoinRequest(buf_str);
            }
            else if(type_code == ALLJOYN_CODE_JOIN_RESPONSE)
            {
                xmppConnector->HandleIncomingJoinResponse(buf_str);
            }
            else if(type_code == ALLJOYN_CODE_SESSION_JOINED)
            {
                xmppConnector->HandleIncomingSessionJoined(buf_str);
            }
            else if(type_code == ALLJOYN_CODE_ANNOUNCE)
            {
                xmppConnector->HandleIncomingAnnounce(buf_str);
            }
            else if(type_code == TR069_ALARM_MESSAGE)
            {
                xmppConnector->HandleIncomingAlarm(buf_str);
            }
            else if(type_code == ALLJOYN_CODE_GET_PROPERTY)
            {
                xmppConnector->HandleIncomingGetRequest(buf_str);
            }
            else if(type_code == ALLJOYN_CODE_GET_PROP_REPLY)
            {
                xmppConnector->HandleIncomingGetReply(buf_str);
            }
            else if(type_code == ALLJOYN_CODE_GET_ALL)
            {
                xmppConnector->HandleIncomingGetAll(buf_str);
            }
            else if(type_code == ALLJOYN_CODE_GET_ALL_REPLY)
            {
                xmppConnector->HandleIncomingGetAllReply(buf_str);
            }
        }
        else
        {
            cout << "Could not parse body from XMPP message." << endl;
            return true;
        }
    }

    return 1;
}

void
XMPPConnector::XmppConnectionHandler(
    xmpp_conn_t* const         conn,
    const xmpp_conn_event_t    event,
    const int                  error,
    xmpp_stream_error_t* const streamError,
    void* const                userdata
    )
{
    XMPPConnector* xmppConnector = (XMPPConnector*)userdata;
    BusAttachment* bus = xmppConnector->GetBusAttachment();

    if(event == XMPP_CONN_CONNECT)
    {
        // Send presence to chatroom
        xmpp_stanza_t* message = 0;
        message = xmpp_stanza_new(xmpp_conn_get_context(conn));
        xmpp_stanza_set_name(message, "presence");
        xmpp_stanza_set_attribute(message, "from",
                xmppConnector->GetJabberId().c_str());
        xmpp_stanza_set_attribute(message, "to",
                (xmppConnector->GetChatroomJabberId()+"/"+
                bus->GetGlobalGUIDString().c_str()).c_str());

        char* buf = 0;
        size_t buflen = 0;
        xmpp_stanza_to_text(message, &buf, &buflen);
        cout << "Sending XMPP presence message" << endl;
        free(buf);

        xmpp_send(conn, message);
        xmpp_stanza_release(message);

        // Start listening for advertisements
        QStatus err = bus->FindAdvertisedName("");
        if(err != ER_OK)
        {
            cout << "Could not find advertised names: " <<
                    QCC_StatusText(err) << endl;
        }

        // Listen for announcements
        err = AnnouncementRegistrar::RegisterAnnounceHandler(*bus,
                *((AllJoynHandler*)(xmppConnector->GetBusListener())), NULL, 0);
    }
    else
    {
        cout << "Disconnected from XMPP server." << endl;

        // Stop listening for advertisements
        bus->CancelFindAdvertisedName("");

        // Stop the XMPP event loop
        xmpp_stop(xmpp_conn_get_context(conn));
    }

    // TODO: send connection status to Gateway Management app
}
