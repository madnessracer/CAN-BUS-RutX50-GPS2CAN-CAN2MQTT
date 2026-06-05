#ifndef CAN_OTA_INPUT_H
#define CAN_OTA_INPUT_H

char SSIDIn[33];     // Platz für 32 Zeichen + NUL
char PasswordIn[33]; // Platz für 32 Zeichen + NUL

// Hilfsfunktion: trimme Trailing-NonPrintable und setze Termination
static void sanitize_buffer(char *buf, size_t len)
{
  // sicherstellen, dass Puffer terminiert ist
  buf[len] = '\0';

  // finde letztes druckbares Zeichen (ASCII 32..126)
  int last = (int)len - 1;
  while (last >= 0)
  {
    unsigned char c = (unsigned char)buf[last];
    if (c >= 32 && c <= 126)
      break;
    buf[last] = '\0';
    last--;
  }
  // buf ist jetzt null-terminiert und ohne trailing non-printables
}

void CanInputOTA()
{
  // Nur verarbeiten, wenn identifier und Device-ID passt
  if (!(rx_frame.identifier == Can_Input_Wifi && rx_frame.data[0] == OtaDevieID))
    return;

  // Initialisiere Puffer einmalig wenn nötig (setzen auf 0)
  static bool buffers_init = false;
  if (!buffers_init)
  {
    memset(SSIDIn, 0, sizeof(SSIDIn));
    memset(PasswordIn, 0, sizeof(PasswordIn));
    buffers_init = true;
  }

  // Mapping: Passwort Segmente 0x02..0x07 (erste 5 Segmente 6 Bytes, letztes evtl. 2 Bytes)
  //          SSID Segmente     0x09..0x0E (analog)
  uint8_t cmd = rx_frame.data[1];

  auto copy_segment = [&](char *dst, uint8_t baseCmd, size_t totalLen)
  {
    if (cmd < baseCmd)
      return false;
    uint8_t idx = cmd - baseCmd;
    size_t offset = (size_t)idx * 6;

    // Neu: beim ersten Segment gesamten Zielpuffer löschen, damit keine Reste bleiben
    if (idx == 0)
    {
      memset(dst, 0, totalLen + 1); // +1 für Null-Terminator
    }

    // Anzahl zu kopierender Bytes: normalerweise 6, beim letzten Segment evtl. weniger
    size_t toCopy = 6;
    if (offset + toCopy > totalLen)
      toCopy = totalLen > offset ? totalLen - offset : 0;
    for (size_t i = 0; i < toCopy; ++i)
    {
      dst[offset + i] = (char)rx_frame.data[2 + i];
    }

    // Neu: restliche Bytes innerhalb dieses Segments auf 0 setzen (verhindert "Reste")
    for (size_t i = toCopy; i < 6; ++i)
    {
      if (offset + i < totalLen)
        dst[offset + i] = '\0';
    }

    // Wenn dies das letzte Segment (offset+toCopy >= totalLen), sanitize und speichern
    if (offset + toCopy >= totalLen)
    {
      // sicher terminieren und restlichen Bereich nullen (falls noch Reste)
      if (offset + toCopy < totalLen)
      {
        for (size_t i = offset + toCopy; i < totalLen; ++i)
          dst[i] = '\0';
      }
      dst[totalLen] = '\0';
      sanitize_buffer(dst, totalLen);
      return true; // signalisiert "last segment"
    }
    return false;
  };

  // PASSWORD verarbeiten (32 Bytes)
  if (cmd >= 0x02 && cmd <= 0x07)
  {
    bool last = copy_segment(PasswordIn, 0x02, 32);
    if (last)
    {
      delay(100);
      String Passwod = String(PasswordIn);
      Passwod.trim();
      PASSWORD_Schreiben(Passwod);
    }
  }

  // Anzeige-Request: SSID/PW senden ###################################################
  if (cmd == 0x08)
  {
    delay(100);
    Aktuelle_SSID_Senden();
    delay(100);
    Aktuelle_PASSWORT_Senden();
  }
  // ENDE Anzeige-Request: SSID/PW senden #############################################

  // SSID verarbeiten (32 Bytes) (Segmente 0x09..0x0E)
  if (cmd >= 0x09 && cmd <= 0x0E)
  {
    bool last = copy_segment(SSIDIn, 0x09, 32);
    if (last)
    {
      delay(100);
      String SSID = String(SSIDIn);
      SSID.trim();
      SSID_Schreiben(SSID);
    }
  }
}
#endif