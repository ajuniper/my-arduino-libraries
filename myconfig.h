// config access
/*

generic config library

access via web using /config?name=XX&id=YY&value=ZZ

register handler against XX as int/string
if ZZ not provided then response is treated as a GET
handler called with XX, YY, ZZ (ZZ = reference s.t. it can be overridden)
handler returns NULL/error message to indicate acceptance
saves in prefs as XX.YY

*/

// must be called first
extern void MyCfgInit(const char * ns = "myconfig");

// callback types for setting int and string
// return is either NULL for OK or error message to return
typedef const char * (*MyCfgCbInt)(const char * name, const String & id, int &value);
typedef const char * (*MyCfgCbString)(const char * name, const String & id, String &value);

// register interest in a config set of "name"
extern void MyCfgRegisterInt(const char * name, MyCfgCbInt cb);
extern void MyCfgRegisterString(const char * name, MyCfgCbString cb);

// retrieve a config value of "id" inside "name"
extern int MyCfgGetInt(const char * name, const String & id, int def);
extern String MyCfgGetString(const char * name, const String & id, const String & def);

// set a config value of "id" inside "name"
extern bool MyCfgPutInt(const char * name, const String & id, int value);
extern bool MyCfgPutString(const char * name, const String & id, const String & value);
