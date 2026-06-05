#ifndef __FILESYSTEM_H__
#define __FILESYSTEM_H__

#define FORMAT_LITTLEFS_IF_FAILED true

static char FileBuffer[32];
static String SSID;
static String PASSWORD;

static bool g_LittleFS_mounted = false;

static inline bool FS_Open()
{
    if (g_LittleFS_mounted)
    {
        return true;
    }

    if (!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED))
    {
        Serial.println("LittleFS Mount Failed");
        g_LittleFS_mounted = false;
        return false;
    }

    g_LittleFS_mounted = true;
    delay(100);
    return true;
}

static inline void FS_Close()
{
    if (g_LittleFS_mounted)
    {
        LittleFS.end();
        g_LittleFS_mounted = false;
    }
    delay(100);
}

static inline void listDir(fs::FS &fs, const char *dirname, uint8_t levels)
{
    Serial.printf("Listing directory: %s\r\n", dirname);

    File root = fs.open(dirname);
    if (!root)
    {
        Serial.println("- failed to open directory");
        return;
    }
    if (!root.isDirectory())
    {
        Serial.println(" - not a directory");
        return;
    }

    File file = root.openNextFile();
    while (file)
    {
        if (file.isDirectory())
        {
            Serial.print("  DIR : ");
            Serial.println(file.name());
            if (levels)
            {
                listDir(fs, file.path(), levels - 1);
            }
        }
        else
        {
            Serial.print("  FILE: ");
            Serial.print(file.name());
            Serial.print("\tSIZE: ");
            Serial.println(file.size());
        }
        file = root.openNextFile();
    }
}

static inline void createDir(fs::FS &fs, const char *path)
{
    Serial.printf("Creating Dir: %s\n", path);
    if (fs.mkdir(path))
    {
        Serial.println("Dir created");
    }
    else
    {
        Serial.println("mkdir failed");
    }
}

static inline void removeDir(fs::FS &fs, const char *path)
{
    Serial.printf("Removing Dir: %s\n", path);
    if (fs.rmdir(path))
    {
        Serial.println("Dir removed");
    }
    else
    {
        Serial.println("rmdir failed");
    }
}

static inline void readFile(fs::FS &fs, const char *path)
{
    if (!fs.exists(path))
    {
        File file = fs.open(path, FILE_WRITE);
        if (file)
        {
            file.close();
        }
        FileBuffer[0] = '\0';
        return;
    }

    File file = fs.open(path, FILE_READ);

    if (!file || file.isDirectory())
    {
        FileBuffer[0] = '\0';
        return;
    }

    while (file.available())
    {
        int l = file.readBytesUntil('\n', FileBuffer, sizeof(FileBuffer));
        FileBuffer[l] = 0;
    }

    file.close();
}

static inline void writeFile(fs::FS &fs, const char *path, const char *message)
{
    //Serial.printf("Writing file: %s\r\n", path);

    File file = fs.open(path, FILE_WRITE);
    if (!file)
    {
        Serial.println("- failed to open file for writing");
        return;
    }
    if (file.print(message))
    {
       // Serial.println("- file written");
    }
    else
    {
        Serial.println("- write failed");
    }
    file.close();
}

static inline void appendFile(fs::FS &fs, const char *path, const char *message)
{
    Serial.printf("Appending to file: %s\r\n", path);

    File file = fs.open(path, FILE_APPEND);
    if (!file)
    {
        Serial.println("- failed to open file for appending");
        return;
    }
    if (file.print(message))
    {
        Serial.println("- message appended");
    }
    else
    {
        Serial.println("- append failed");
    }
    file.close();
}

static inline void renameFile(fs::FS &fs, const char *path1, const char *path2)
{
    Serial.printf("Renaming file %s to %s\r\n", path1, path2);
    if (fs.rename(path1, path2))
    {
        Serial.println("- file renamed");
    }
    else
    {
        Serial.println("- rename failed");
    }
}

static inline void deleteFile(fs::FS &fs, const char *path)
{
    Serial.printf("Deleting file: %s\r\n", path);
    if (fs.remove(path))
    {
        Serial.println("- file deleted");
    }
    else
    {
        Serial.println("- delete failed");
    }
}

static inline void testFileIO(fs::FS &fs, const char *path)
{
    Serial.printf("Testing file I/O with %s\r\n", path);

    static uint8_t buf[512];
    size_t len = 0;
    File file = fs.open(path, FILE_WRITE);
    if (!file)
    {
        Serial.println("- failed to open file for writing");
        return;
    }

    size_t i;
    Serial.print("- writing");
    uint32_t start = millis();
    for (i = 0; i < 2048; i++)
    {
        if ((i & 0x001F) == 0x001F)
        {
            Serial.print(".");
        }
        file.write(buf, 512);
    }
    Serial.println("");
    uint32_t end = millis() - start;
    Serial.printf(" - %u bytes written in %u ms\r\n", 2048 * 512, end);
    file.close();

    file = fs.open(path);
    start = millis();
    end = start;
    i = 0;
    if (file && !file.isDirectory())
    {
        len = file.size();
        size_t flen = len;
        start = millis();
        Serial.print("- reading");
        while (len)
        {
            size_t toRead = len;
            if (toRead > 512)
            {
                toRead = 512;
            }
            file.read(buf, toRead);
            if ((i++ & 0x001F) == 0x001F)
            {
                Serial.print(".");
            }
            len -= toRead;
        }
        Serial.println("");
        end = millis() - start;
        Serial.printf("- %u bytes read in %u ms\r\n", flen, end);
        file.close();
    }
    else
    {
        Serial.println("- failed to open file for reading");
    }
}

static inline String SSID_Lesen()
{
    FS_Open();
    readFile(LittleFS, "/SSID.txt");
    FS_Close();
    return FileBuffer;
}

static inline String PASSWORD_Lesen()
{
    FS_Open();
    readFile(LittleFS, "/PASSWORD.txt");
    FS_Close();
    return FileBuffer;
}

static inline void SSID_Schreiben(String Wert)
{
    char arr[33];
    Wert.toCharArray(arr, sizeof(arr));
    FS_Open();
    writeFile(LittleFS, "/SSID.txt", arr);
    FS_Close();

    Serial.println(SSID_Lesen());
    Serial.println();
}

static inline void PASSWORD_Schreiben(String Wert)
{
    char arr[33];
    Wert.toCharArray(arr, sizeof(arr));
    FS_Open();
    writeFile(LittleFS, "/PASSWORD.txt", arr);
    FS_Close();

    Serial.println(PASSWORD_Lesen());
    Serial.println();
}

// Firmware-Version-Zähler (erhöht bei OTA-Update)
static inline uint32_t getFirmwareVersion()
{
    FS_Open();
    readFile(LittleFS, "/firmware_version.txt");
    FS_Close();
    uint32_t version = atoi(FileBuffer);
    return version > 0 ? version : 0; // Default 0
}

static inline void incrementFirmwareVersion()
{
    uint32_t version = getFirmwareVersion() + 1;
    char buf[16];
    sprintf(buf, "%lu", version);
    FS_Open();
    writeFile(LittleFS, "/firmware_version.txt", buf);
    FS_Close();
    //Serial.printf("Firmware-Version erhöht auf: %lu\n", version);
}

// Boot-Zähler (erhöht bei jedem Boot)
static inline uint32_t getBootCount()
{
    FS_Open();
    readFile(LittleFS, "/boot_count.txt");
    FS_Close();
    uint32_t count = atoi(FileBuffer);
    return count > 0 ? count : 0; // Default 0
}

static inline void incrementBootCount()
{
    uint32_t count = getBootCount() + 1;
    char buf[16];
    sprintf(buf, "%lu", count);
    FS_Open();
    writeFile(LittleFS, "/boot_count.txt", buf);
    FS_Close();
    //Serial.printf("Boot-Zähler erhöht auf: %lu\n", count);
}

#endif