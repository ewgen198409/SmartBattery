/*
 * SmartBattery.ino (https://github.com/dsa-a/Arduino-Smart-Battery)
 * Copyright (C) 2021, Andrei Egorov
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <Wire.h>                // Включаем библиотеку Wire для I2C (SMBus) связи.
                                 // !!! ВАЖНО: Библиотека WIRE ДОЛЖНА быть изменена для увеличения буфера до 34 (0x22) байт !!!
                                 // !!! Стандартную библиотеку Arduino Wire необходимо изменить в двух местах - Wire.h и utility/twi.h !!!

//#define debug                  // Раскомментируйте эту строку, чтобы включить отладочный вывод (например, расчет PEC)

#define addr 0x0B                // Адрес SMBus микросхемы Smart Battery по умолчанию.

#define new_capacity 4400        // Определяет новую расчетную емкость, которая будет записана в батарею (в мАч).

#define unseal_key_1 0x0414      // Первая часть ключа разблокировки. Специфична для микросхемы батареи.
#define unseal_key_2 0x3672      // Вторая часть ключа разблокировки.

#define full_access_key_1 0xFFFF // Первая часть ключа полного доступа.
#define full_access_key_2 0xFFFF // Вторая часть ключа полного доступа. (Часто 0xFFFF 0xFFFF по умолчанию)

#define pf_clear_key_1 0x2673    // Первая часть ключа очистки постоянной ошибки (PF).
#define pf_clear_key_2 0x1712    // Вторая часть ключа очистки постоянной ошибки (PF).

// Макросы для разбора даты компиляции (__DATE__) на год, месяц и день.
// Это позволяет Arduino автоматически устанавливать текущую дату на микросхеме батареи.
#define year (__DATE__[7]-'0')*1000+(__DATE__[8]-'0')*100+(__DATE__[9]-'0')*10+(__DATE__[10]-'0')
#define month (__DATE__[0] == 'J' && __DATE__[1] == 'a')  ? 1 : \
            (__DATE__[0] == 'F')                          ? 2 : \
            (__DATE__[0] == 'M' && __DATE__[2] == 'r')  ? 3 : \
            (__DATE__[0] == 'A' && __DATE__[1] == 'p')  ? 4 : \
            (__DATE__[0] == 'M' && __DATE__[2] == 'y')  ? 5 : \
            (__DATE__[0] == 'J' && __DATE__[2] == 'n')  ? 6 : \
            (__DATE__[0] == 'J' && __DATE__[2] == 'l')  ? 7 : \
            (__DATE__[0] == 'A' && __DATE__[1] == 'u')  ? 8 : \
            (__DATE__[0] == 'S')                          ? 9 : \
            (__DATE__[0] == 'O')                          ? 10 : \
            (__DATE__[0] == 'N')                          ? 11 : \
            (__DATE__[0] == 'D')                          ? 12 : 0
#define day (((__DATE__[4] >= '0') ? (__DATE__[4]) : '0')-'0')*10+(__DATE__[5]-'0')

byte buff[34]; // Глобальный буфер для хранения данных, прочитанных из SMBus. Максимальный размер 34 байта согласно изменению буфера Wire.

#if defined (debug)
byte sp; // Переменная для расчета PEC в режиме отладки.

// Функция для расчета Packet Error Code (PEC) для SMBus.
// PEC - это расчет CRC-8, используемый для проверки ошибок.
byte PEC(byte p, byte b) {
  b^=p; // Исключающее ИЛИ с предыдущим байтом PEC.
  for (byte i=0; i<8; i++ ) {
    byte t=b&0x80; // Проверяем старший бит.
    b<<=1;        // Сдвигаем байт влево на 1 бит.
    if (t!=0) b^=0x07; // Если старший бит был установлен, выполняем XOR с полиномом (0x07 для CRC-8).
  }
  return b; // Возвращаем рассчитанный PEC.
}
#endif

// Функция для проверки статуса передачи по Wire (I2C/SMBus).
// Если статус не равен 0 (т.е. есть ошибка), выводит сообщение об ошибке и останавливает программу.
void CheckWireStatus(byte wire_status) {
  if (wire_status!=0) {
    Serial.print(F("Wire error - ")); // Выводим сообщение об ошибке Wire.
    Serial.println(wire_status);      // Выводим код ошибки.
    while (true) ;                    // Останавливаем выполнение программы.
  }
}

// Функция для отправки команды SMBus.
// Отправляет байт команды на адрес батареи без завершения транзакции (т.е., keeps the connection open).
void SMBCommand(byte comm) {
  Wire.beginTransmission(addr);         // Начинаем передачу на адрес батареи.
  Wire.write(comm);                     // Записываем байт команды.
  CheckWireStatus(Wire.endTransmission(false)); // Завершаем передачу, но не освобождаем шину (false).
}

// Функция для чтения N байт из SMBus.
// Читает N+1 байт (N байт данных + 1 байт PEC, если включена отладка) и сохраняет их в буфер.
void Read(byte n) {
  Wire.requestFrom(addr,n+1); // Запрашиваем N+1 байт от SMBus-устройства.
#if defined (debug)
  byte p=PEC(sp, (addr<<1)+1); // В режиме отладки рассчитываем PEC для чтения (адрес + 1 (для чтения)).
#endif
  byte b=Wire.available();    // Получаем количество доступных байт.
  for (byte i=0; i<b; i++) {
    buff[i]=Wire.read();    // Читаем байт и сохраняем его в буфер.
#if defined (debug)
    if (i<(b-1)) p=PEC(p, buff[i]); // В режиме отладки обновляем PEC, если это не последний байт (PEC-байт).
    printHEX(buff[i]);      // Печатаем байт в HEX формате.
    Serial.print(",");      // Разделитель.
#endif
  }
#if defined (debug)
  printHEX(p);            // В режиме отладки печатаем рассчитанный PEC.
  Serial.println();       // Новая строка.
#endif
}

// Функция для чтения данных по команде SMBus (короткий формат).
// Используется для чтения 2-байтных данных (word) из регистров SMBus.
void ReadSMB(byte comm) {
  SMBCommand(comm); // Отправляем команду.
#if defined (debug)
  sp=PEC(PEC(0, addr<<1), comm); // В режиме отладки рассчитываем начальный PEC для записи (адрес + команда).
#endif
  Read(2); // Читаем 2 байта данных.
}

// Перегруженная функция для чтения данных по команде SMBus (расширенный формат, использующий команду 0x00).
// Некоторые команды SMBus требуют записи 2-байтного аргумента после команды 0x00.
void ReadSMB(word comm) {
  Wire.beginTransmission(addr); // Начинаем передачу.
  Wire.write(00);               // Отправляем команду 0x00 (ProcessCall, ReadWord, WriteWord, BlockRead/Write).
  Wire.write(lowByte(comm));    // Отправляем младший байт команды/аргумента.
  Wire.write(highByte(comm));   // Отправляем старший байт команды/аргумента.
  CheckWireStatus(Wire.endTransmission()); // Завершаем передачу.
  ReadSMB(byte(0x00));          // Затем читаем данные, связанные с этой командой (обычно через команду 0x00).
}

// Функция для чтения блочных данных из SMBus.
// Сначала считывается длина блока, затем сам блок данных.
void ReadBlockSMB(byte comm) {
  SMBCommand(comm);           // Отправляем команду для блочного чтения.
  Wire.requestFrom(addr,1);   // Запрашиваем 1 байт (длину блока).
  byte b=Wire.read();         // Читаем длину блока.
  SMBCommand(comm);           // Отправляем команду снова (необходимо для некоторых SMBus устройств).
#if defined (debug)
  sp=PEC(PEC(0, addr<<1), comm); // В режиме отладки рассчитываем начальный PEC.
#endif
  Read(b+1);                  // Читаем данные: b байт данных + 1 байт PEC (если есть).
}

// Функция для записи 2-байтных данных (word) в SMBus.
void WriteSMBWord(byte comm, word data) {
  Wire.beginTransmission(addr); // Начинаем передачу.
  Wire.write(comm);             // Отправляем команду.
  Wire.write(lowByte(data));    // Отправляем младший байт данных.
  Wire.write(highByte(data));   // Отправляем старший байт данных.
  CheckWireStatus(Wire.endTransmission()); // Завершаем передачу.
}

// Функция для чтения данных из подкласса SMBus.
// Используется для доступа к расширенным регистрам данных, которые сгруппированы по "подклассам" или "страницам".
void ReadSMBSubclass(byte id, byte page) {
  WriteSMBWord(0x77, word(id)); // Записываем ID подкласса в регистр 0x77 (ManufacturerAccess).
  delay(100);                   // Небольшая задержка для стабилизации.
  ReadBlockSMB(page);           // Затем читаем данные из указанной "страницы" (регистра).
}

// Функция для записи данных в подкласс SMBus.
void WriteSMBSubclass(byte id, byte page) {
  WriteSMBWord(0x77, word(id)); // Записываем ID подкласса в регистр 0x77 (ManufacturerAccess).
  delay(100);                   // Небольшая задержка.
  Wire.beginTransmission(addr); // Начинаем передачу.
  Wire.write(page);             // Отправляем номер страницы/регистра.
  for (byte i=0;i<=buff[0];i++) Wire.write(buff[i]); // Записываем данные из буфера (buff[0] содержит длину).
  CheckWireStatus(Wire.endTransmission()); // Завершаем передачу.
  delay(100);                   // Небольшая задержка.
}

// Вспомогательная функция для печати байта в шестнадцатеричном формате с ведущим нулем.
void printHEX(byte b) {
  if (b<16) Serial.print("0"); // Если число меньше 16, добавляем ведущий ноль.
  Serial.print(b,HEX);         // Печатаем байт в шестнадцатеричном формате.
}

// Вспомогательная функция для печати содержимого блочных данных (ASCII-строки).
void printBlock() {
  for (byte i=1; i<=buff[0]; i++) Serial.print(char(buff[i])); // buff[0] содержит длину блока, начиная с buff[1] - данные.
  Serial.println(); // Новая строка.
}

// Читает и печатает информацию о типе устройства, версии прошивки и версии оборудования.
void Read123() {
  ReadSMB(word(0x0001)); Serial.print(F("Device Type: ")); printHEX(buff[1]); printHEX(buff[0]); Serial.println(" Hex"); // Тип устройства
  ReadSMB(word(0x0002)); Serial.print(F("Firmware Version: ")); printHEX(buff[1]); printHEX(buff[0]); Serial.println(" Hex"); // Версия прошивки
  ReadSMB(word(0x0003)); Serial.print(F("Hardware Version: ")); printHEX(buff[1]); printHEX(buff[0]); Serial.println(" Hex"); // Версия оборудования
}

// Основная функция для получения и вывода всей доступной информации о батарее.
void info() {
  ReadSMB(byte(0x18)); Serial.print(F("DesignCapacity: ")); Serial.print(buff[1]*256+buff[0]); Serial.println(" mAh"); // Расчетная емкость
  ReadSMB(byte(0x10)); Serial.print(F("FullChargeCapacity: ")); Serial.print(buff[1]*256+buff[0]); Serial.println(" mAh"); // Полная емкость заряда
  ReadSMB(byte(0x17)); Serial.print(F("CycleCount: ")); Serial.println(buff[1]*256+buff[0]); // Количество циклов
  // Дата производства (год, месяц, день)
  ReadSMB(byte(0x1B)); Serial.print(F("Date: ")); Serial.print(1980+(buff[1]>>1)); Serial.print("."); Serial.print(((buff[1]&0b00000001)<<3)+(buff[0]>>5));Serial.print("."); Serial.println(buff[0]&0b00011111);
  ReadSMB(byte(0x19)); Serial.print(F("DesignVoltage: ")); Serial.print(buff[1]*256+buff[0]); Serial.println(" mV"); // Расчетное напряжение
  ReadBlockSMB(0x20); Serial.print(F("ManufName: ")); printBlock(); // Имя производителя
  ReadBlockSMB(0x21); Serial.print(F("DeviceName: ")); printBlock(); // Имя устройства
  ReadSMB(byte(0x1C)); Serial.print(F("SerialNumber: ")); printHEX(buff[1]); printHEX(buff[0]); Serial.println(" Hex"); // Серийный номер
  ReadSMB(byte(0x14)); Serial.print(F("ChargingCurrent: ")); Serial.print(buff[1]*256+buff[0]); Serial.println(" mA"); // Ток зарядки
  ReadSMB(byte(0x15)); Serial.print(F("ChargingVoltage: ")); Serial.print(buff[1]*256+buff[0]); Serial.println(" mV"); // Напряжение зарядки
  ReadBlockSMB(0x22); Serial.print(F("DeviceChemistry: ")); printBlock(); // Химический состав устройства
  ReadSMB(byte(0x08)); Serial.print(F("Temperature: ")); Serial.print(float(buff[1]*256+buff[0])/10-273); Serial.println(" C"); // Температура (из Кельвинов в Цельсии)
  ReadSMB(byte(0x09)); Serial.print(F("Voltage: ")); Serial.print(buff[1]*256+buff[0]); Serial.println(" mV"); // Напряжение
  ReadSMB(byte(0x0A)); Serial.print(F("Current: ")); Serial.print(int(buff[1]*256+buff[0])); Serial.println(" mA"); // Ток
  ReadSMB(byte(0x0D)); Serial.print(F("RelativeSOC: ")); Serial.print(buff[0]); Serial.println(" %"); // Относительный SOC
  ReadSMB(byte(0x0E)); Serial.print(F("AbsoluteSOC: ")); Serial.print(buff[0]); Serial.println(" %"); // Абсолютный SOC
  ReadSMB(byte(0x0F)); Serial.print(F("RemainingCapacity: ")); Serial.print(buff[1]*256+buff[0]); Serial.println(" mAh"); // Остаточная емкость
  ReadSMB(byte(0x3C)); Serial.print(F("VCELL4: ")); Serial.print(buff[1]*256+buff[0]); Serial.println(" mV"); // Напряжение ячейки 4
  ReadSMB(byte(0x3D)); Serial.print(F("VCELL3: ")); Serial.print(buff[1]*256+buff[0]); Serial.println(" mV"); // Напряжение ячейки 3
  ReadSMB(byte(0x3E)); Serial.print(F("VCELL2: ")); Serial.print(buff[1]*256+buff[0]); Serial.println(" mV"); // Напряжение ячейки 2
  ReadSMB(byte(0x3F)); Serial.print(F("VCELL1: ")); Serial.print(buff[1]*256+buff[0]); Serial.println(" mV"); // Напряжение ячейки 1
  ReadSMB(byte(0x1A)); Serial.print(F("SpecificationInfo: ")); printHEX(buff[1]); printHEX(buff[0]); Serial.println(" Hex"); // Информация о спецификации
  ReadSMB(byte(0x16)); Serial.print(F("Battery Status: ")); printHEX(buff[1]); printHEX(buff[0]); Serial.println(" Hex"); // Статус батареи
  // Разбор битов статуса батареи
  if (buff[1]&0b10000000) Serial.print("OCA|"); // Overcharge Alarm
  if (buff[1]&0b01000000) Serial.print("TCA|"); // Terminate Charge Alarm
  if (buff[1]&0b00010000) Serial.print("OTA|"); // Overtemperature Alarm
  if (buff[1]&0b00001000) Serial.print("TDA|"); // Terminate Discharge Alarm
  if (buff[1]&0b00000010) Serial.print("RCA|"); // Remaining Capacity Alarm
  if (buff[1]&0b00000001) Serial.print("RTA|"); // Remaining Time Alarm
  if (buff[0]&0b10000000) Serial.print("INIT|"); // Initialized
  if (buff[0]&0b01000000) Serial.print("DSG|"); // Discharging
  if (buff[0]&0b00100000) Serial.print("FC|");  // Fully Charged
  if (buff[0]&0b00010000) Serial.print("FD|");  // Fully Discharged
  if (buff[0]&0b00001000) Serial.print("EC3|"); // Error Code 3
  if (buff[0]&0b00000100) Serial.print("EC2|"); // Error Code 2
  if (buff[0]&0b00000010) Serial.print("EC1|"); // Error Code 1
  if (buff[0]&0b00000001) Serial.print("EC0|"); // Error Code 0
  Serial.println();
  ReadSMB(word(0x0054)); Serial.print(F("Operation Status: ")); printHEX(buff[1]); printHEX(buff[0]); Serial.println(" Hex"); // Статус операции
  // Разбор битов статуса операции
  if (buff[1]&0b10000000) Serial.print("PRES|"); // Primary Register Set
  if (buff[1]&0b01000000) Serial.print("FAS|");  // Full Access Sealed (sealed but with full access)
  if (buff[1]&0b00100000) Serial.print("SS|");   // Sealed Status
  if (buff[1]&0b00010000) Serial.print("CSV|");  // Control Status Valid
  if (buff[1]&0b00000100) Serial.print("LDMD|"); // Load Mode
  if (buff[0]&0b10000000) Serial.print("WAKE|"); // Wake Up
  if (buff[0]&0b01000000) Serial.print("DSG|");  // Discharging
  if (buff[0]&0b00100000) Serial.print("XDSG|"); // XDISG (Discharge protection)
  if (buff[0]&0b00010000) Serial.print("XDSGI|"); // XDISGI (Intermediate Discharge protection)
  if (buff[0]&0b00000100) Serial.print("R_DIS|"); // Relative Discharge
  if (buff[0]&0b00000010) Serial.print("VOK|");  // Voltage OK
  if (buff[0]&0b00000001) Serial.print("QEN|");  // QMAX Enable
  Serial.println();
  // Проверяем, запечатана ли батарея
  if (buff[1]&0b00100000) { // Если бит SS (Sealed Status) установлен
    Serial.println(F("Sealed")); // Батарея запечатана
    Read123(); // Читаем базовую информацию
  } else {
    Serial.println(F("Unsealed")); // Батарея разблокирована
    if (!(buff[1]&0b01000000)) { // Если бит FAS (Full Access Sealed) не установлен (т.е. в полном доступе)
      Serial.println(F("Pack in Full Access mode")); // Батарея в режиме полного доступа
      ReadBlockSMB(0x60); Serial.print(F("UnSealKeys: 0x")); printHEX(buff[2]); printHEX(buff[1]); Serial.print(F(", 0x")); printHEX(buff[4]); printHEX(buff[3]); Serial.println(" Hex"); // Ключи разблокировки
      ReadBlockSMB(0x61); Serial.print(F("FullAccessKeys: 0x")); printHEX(buff[2]); printHEX(buff[1]); Serial.print(F(", 0x")); printHEX(buff[4]); printHEX(buff[3]); Serial.println(" Hex"); // Ключи полного доступа
      ReadBlockSMB(0x62); Serial.print(F("PFKeys: 0x")); printHEX(buff[2]); printHEX(buff[1]); Serial.print(F(", 0x")); printHEX(buff[4]); printHEX(buff[3]); Serial.println(" Hex"); // Ключи PF
    };
    ReadSMB(byte(0x0C)); Serial.print(F("MaxError: ")); Serial.print(buff[0]); Serial.println(" %"); // Максимальная ошибка SOC
    ReadSMB(word(0x0051)); Serial.print(F("SafetyStatus: ")); // Статус безопасности
    if ((buff[1]==0)&&(buff[0]==0)) {
      Serial.println("OK"); // Статус OK
    } else {
      printHEX(buff[1]); printHEX(buff[0]); Serial.println(" Hex"); // Выводим байты статуса
      // Разбор битов статуса безопасности
      if (buff[1]&0b10000000) Serial.print("OTD|");  // Overtemperature During Discharge
      if (buff[1]&0b01000000) Serial.print("OTC|");  // Overtemperature During Charge
      if (buff[1]&0b00100000) Serial.print("OCD|");  // Overcurrent During Discharge
      if (buff[1]&0b00010000) Serial.print("OCC|");  // Overcurrent During Charge
      if (buff[1]&0b00001000) Serial.print("OCD2|"); // Overcurrent During Discharge 2
      if (buff[1]&0b00000100) Serial.print("OCC2|"); // Overcurrent During Charge 2
      if (buff[1]&0b00000010) Serial.print("PUV|");  // Pack Undervoltage
      if (buff[1]&0b00000001) Serial.print("POV|");  // Pack Overvoltage
      if (buff[0]&0b10000000) Serial.print("CUV|");  // Cell Undervoltage
      if (buff[0]&0b01000000) Serial.print("COV|");  // Cell Overvoltage
      if (buff[0]&0b00100000) Serial.print("PF|");   // Permanent Failure
      if (buff[0]&0b00010000) Serial.print("HWDG|"); // Hardware Watchdog
      if (buff[0]&0b00001000) Serial.print("WDF|");  // Watchdog Fault
      if (buff[0]&0b00000100) Serial.print("AOCD|"); // AFE Overcurrent During Discharge
      if (buff[0]&0b00000010) Serial.print("SCC|");  // Short Circuit During Charge
      if (buff[0]&0b00000001) Serial.print("SCD|");  // Short Circuit During Discharge
      Serial.println();
    };
    ReadSMB(word(0x0053)); Serial.print(F("PFStatus: ")); // Статус постоянной ошибки
    if ((buff[1]==0)&&(buff[0]==0)) {
      Serial.println("OK"); // Статус OK
    } else {
      printHEX(buff[1]); printHEX(buff[0]); Serial.println(" Hex"); // Выводим байты статуса
      // Разбор битов статуса постоянной ошибки
      if (buff[1]&0b10000000) Serial.print("FBF|");   // Fuse Blown Fault
      if (buff[1]&0b00010000) Serial.print("SOPT|");  // Safety Option Timeout
      if (buff[1]&0b00001000) Serial.print("SOCD|");  // Safety Overcurrent During Discharge
      if (buff[1]&0b00000100) Serial.print("SOCC|");  // Safety Overcurrent During Charge
      if (buff[1]&0b00000010) Serial.print("AFE_P|"); // AFE Permanent Fault
      if (buff[1]&0b00000001) Serial.print("AFE_C|"); // AFE Communication Fault
      if (buff[0]&0b10000000) Serial.print("DFF|");   // Data Flash Fault
      if (buff[0]&0b01000000) Serial.print("DFETF|"); // Discharge FET Fault
      if (buff[0]&0b00100000) Serial.print("CFETF|"); // Charge FET Fault
      if (buff[0]&0b00010000) Serial.print("CIM|");   // Cell Imbalance
      if (buff[0]&0b00001000) Serial.print("SOTD|");  // Safety Overtemperature During Discharge
      if (buff[0]&0b00000100) Serial.print("SOTC|");  // Safety Overtemperature During Charge
      if (buff[0]&0b00000010) Serial.print("SOV|");   // Safety Overvoltage
      if (buff[0]&0b00000001) Serial.print("PFIN|");  // Primary Failure Input
      Serial.println();
    };
    ReadSMB(word(0x0055)); Serial.print(F("Charging Status: ")); // Статус зарядки
    if ((buff[1]==0)&&(buff[0]==0)) {
      Serial.println("OK"); // Статус OK
    } else {
      printHEX(buff[1]); printHEX(buff[0]); Serial.println(" Hex"); // Выводим байты статуса
      // Разбор битов статуса зарядки
      if (buff[1]&0b10000000) Serial.print("XCHG|");   // External Charge (charger connected)
      if (buff[1]&0b01000000) Serial.print("CHGSUSP|"); // Charge Suspend
      if (buff[1]&0b00100000) Serial.print("PCHG|");   // Pre-Charge
      if (buff[1]&0b00010000) Serial.print("MCHG|");   // Main Charge
      if (buff[1]&0b00001000) Serial.print("TCHG1|");  // Charge Timeout 1
      if (buff[1]&0b00000100) Serial.print("TCHG2|");  // Charge Timeout 2
      if (buff[1]&0b00000010) Serial.print("FCHG|");   // Fast Charge
      if (buff[1]&0b00000001) Serial.print("PULSE|");  // Pulse Charge
      if (buff[0]&0b10000000) Serial.print("PLSOFF|"); // Pulse Off
      if (buff[0]&0b01000000) Serial.print("CB|");     // Cell Balancing Active
      if (buff[0]&0b00100000) Serial.print("PCMTO|");  // Pre-Charge Timeout
      if (buff[0]&0b00010000) Serial.print("FCMTO|");  // Fast Charge Timeout
      if (buff[0]&0b00001000) Serial.print("OCHGV|");  // Overcharge Voltage
      if (buff[0]&0b00000100) Serial.print("OCHGI|");  // Overcharge Current
      if (buff[0]&0b00000010) Serial.print("OC|");     // Overcurrent
      if (buff[0]&0b00000001) Serial.print("XCHGLV|"); // External Charge Low Voltage
      Serial.println();
    };
    ReadSMB(byte(0x46)); Serial.print(F("FETControl: ")); // Управление FET (полевыми транзисторами)
    if (buff[0]==0) {
      Serial.println("OK"); // Статус OK
    } else {
      printHEX(buff[0]); Serial.println(" Hex"); // Выводим байт статуса
      // Разбор битов управления FET
      if (buff[0]&0b00010000) Serial.print("OD|");    // FET On-Demand
      if (buff[0]&0b00001000) Serial.print("ZVCHG|"); // Zero-Volt Charge
      if (buff[0]&0b00000100) Serial.print("CHG|");   // Charge FET Enabled
      if (buff[0]&0b00000010) Serial.print("DSG|");   // Discharge FET Enabled
      Serial.println();
    };
    ReadSMBSubclass(82,0x78); // Читаем подкласс 82, страница 0x78 (обычно содержит параметры калибровки)
    Serial.print(F("Update Status: ")); Serial.println(buff[13]); // Статус обновления
    Serial.print(F("Qmax Cell0: ")); Serial.println(buff[1]*256+buff[2]); // Qmax для ячейки 0
    Serial.print(F("Qmax Cell1: ")); Serial.println(buff[3]*256+buff[4]); // Qmax для ячейки 1
    Serial.print(F("Qmax Cell2: ")); Serial.println(buff[5]*256+buff[6]); // Qmax для ячейки 2
    Serial.print(F("Qmax Cell3: ")); Serial.println(buff[7]*256+buff[8]); // Qmax для ячейки 3
    Serial.print(F("Qmax Pack : ")); Serial.println(buff[9]*256+buff[10]); // Qmax для всего пакета
    for (byte i=88; i<=91; i++) { // Цикл для чтения флагов R_a для каждой ячейки
      ReadSMBSubclass(i,0x78); // Читаем подкласс i, страница 0x78
      Serial.print("Cell");
      Serial.print(i-88); // Номер ячейки
      Serial.print(" R_a flag: "); printHEX(buff[1]); printHEX(buff[2]); Serial.println(); // Флаг R_a
    }
    Read123(); // Снова читаем базовую информацию
    ReadSMB(word(0x0006)); Serial.print(F("Manufacturer Status: ")); printHEX(buff[1]); printHEX(buff[0]); Serial.println(" Hex"); // Статус производителя
    // Разбор битов статуса производителя
    if (buff[1]&0b10000000) Serial.print("FET1|");   // FET1 State
    if (buff[1]&0b01000000) Serial.print("FET0|");   // FET0 State
    if (buff[1]&0b00100000) Serial.print("PF1|");    // Permanent Failure 1
    if (buff[1]&0b00010000) Serial.print("PF0|");    // Permanent Failure 0
    if (buff[1]&0b00001000) Serial.print("STATE3|"); // State 3 (custom)
    if (buff[1]&0b00000100) Serial.print("STATE2|"); // State 2 (custom)
    if (buff[1]&0b00000010) Serial.print("STATE1|"); // State 1 (custom)
    if (buff[1]&0b00000001) Serial.print("STATE0|"); // State 0 (custom)
    Serial.println();
    ReadSMB(word(0x0008)); Serial.print(F("Chemistry ID: ")); printHEX(buff[1]); printHEX(buff[0]); Serial.println(" Hex"); // ID химии батареи
    delay(100);
    ReadSMB(byte(0x03)); Serial.print(F("BatteryMode: ")); printHEX(buff[1]); printHEX(buff[0]); Serial.println(" Hex"); // Режим батареи
    // Разбор битов режима батареи
    if (buff[1]&0b10000000) Serial.print("CapM|"); // Capacity Mode (mAh or 10mWh)
    if (buff[1]&0b01000000) Serial.print("ChgM|"); // Charge Mode (constant current or constant power)
    if (buff[1]&0b00100000) Serial.print("AM|");   // Alarm Mode
    if (buff[1]&0b00000010) Serial.print("PB|");   // Primary Battery Support
    if (buff[1]&0b00000001) Serial.print("CC|");   // Charge Current Compensation
    if (buff[0]&0b10000000) Serial.print("CF|");   // Charger Flow (charger control)
    if (buff[0]&0b00000010) Serial.print("PBS|");  // Primary Battery System (system connected to battery)
    if (buff[0]&0b00000001) Serial.print("ICC|");  // Internal Charge Control
    Serial.println();
  };
}

// Функция setup() вызывается один раз при запуске Arduino.
void setup() {
  Wire.begin();     // Инициализируем Wire библиотеку для I2C.
  Serial.begin(9600); // Инициализируем последовательный порт со скоростью 9600 бод.
  Serial.println(F("Arduino Smart Battery")); // Приветственное сообщение.
  Serial.println(F("Несколько утилит для работы с микросхемой TI bq20z...")); // Описание программы.
  Serial.println(F("Нажмите Enter...")); // Просим пользователя нажать Enter.
  while (Serial.available()==0); // Ждем, пока пользователь что-нибудь введет в Serial Monitor.
  Serial.print(F("Проверка связи с устройством по адресу 0x"));
  printHEX(addr); // Печатаем адрес устройства в HEX.
  Serial.println("...");
  byte st; // Переменная для хранения статуса передачи Wire.
  do {
    Wire.beginTransmission(addr); // Начинаем передачу на адрес батареи.
    st=Wire.endTransmission();    // Завершаем передачу и получаем статус.
    if (st!=0) Serial.println(F("Устройство не отвечает.")); delay(1000); // Если ошибка, сообщаем и ждем.
  } while (st!=0); // Повторяем, пока устройство не ответит (статус 0).
  Serial.println(F("Устройство найдено !!!")); // Сообщение об успешном обнаружении.
}

// Функция loop() вызывается повторно после setup().
void loop() {
  delay(1); // Небольшая задержка.
  if (Serial.available()==0) { // Если в последовательном порту нет новых данных
    Serial.println(F("--------------------"));
    Serial.println(F("Выберите операцию:")); // Меню операций.
    Serial.println(F("1. Прочитать информацию о батарее."));
    Serial.println(F("2. Сброс батареи."));
    Serial.println(F("3. Разблокировка батареи."));
    Serial.println(F("4. Перевести батарею в режим полного доступа."));
    Serial.println(F("5. Очистка постоянной ошибки."));
    Serial.println(F("6. Очистка количества циклов."));
    Serial.println(F("7. Установка текущей даты."));
    Serial.println(F("8. Запись DesignCapacity, QMAX, Update status, Ra_table."));
    Serial.println(F("9. Запустить алгоритм отслеживания импеданса."));
    while (Serial.available()==0); // Ждем ввода пользователя.
    switch (Serial.read()) { // Считываем символ, введенный пользователем.
      case 0x31: // '1'
        Serial.println(F("Информация о батарее..."));
        info(); // Вызываем функцию для чтения и отображения информации.
        break;
      case 0x32: // '2'
        WriteSMBWord(0x00,0x0041); // Команда сброса батареи (soft reset).
        Serial.println(F("Сброс..."));
        delay(1000); // Ждем завершения сброса.
        break;
      case 0x33: // '3'
        WriteSMBWord(0x00,unseal_key_1); // Отправляем первую часть ключа разблокировки.
        WriteSMBWord(0x00,unseal_key_2); // Отправляем вторую часть ключа разблокировки.
        Serial.println(F("Разблокировка..."));
        break;
      case 0x34: // '4'
        WriteSMBWord(0x00,full_access_key_1); // Отправляем первую часть ключа полного доступа.
        WriteSMBWord(0x00,full_access_key_2); // Отправляем вторую часть ключа полного доступа.
        Serial.println(F("Переход в режим полного доступа..."));
        break;
      case 0x35: // '5'
        WriteSMBWord(0x00,pf_clear_key_1); // Отправляем первую часть ключа очистки PF.
        WriteSMBWord(0x00,pf_clear_key_2); // Отправляем вторую часть ключа очистки PF.
        Serial.println(F("Очистка постоянной ошибки..."));
        break;
      case 0x36: // '6'
        WriteSMBWord(0x17,0x0000); // Записываем 0 в регистр CycleCount (0x17).
        Serial.println(F("Очистка количества циклов..."));
        break;
      case 0x37: // '7'
        // Устанавливаем текущую дату в регистр 0x1B (Date). Формат: (Год-1980)*512 + Месяц*32 + День.
        WriteSMBWord(0x1B,(year-1980)*512+int(month)*32+day);
        Serial.println(F("Установка текущей даты..."));
        break;
      case 0x38: // '8'
        Serial.println(F("Запись DesignCapacity, QMAX, Update status, Ra_table..."));
        WriteSMBWord(0x18,new_capacity); // Записываем новую расчетную емкость (DesignCapacity) в регистр 0x18.
        delay(100);
        ReadSMBSubclass(82,0x78); // Считываем текущие данные подкласса 82 (Qmax и т.д.)
        // Обновляем значения Qmax для всех ячеек и пакета в буфере на основе new_capacity.
        buff[1]=highByte(new_capacity);
        buff[2]=lowByte(new_capacity);
        buff[3]=highByte(new_capacity);
        buff[4]=lowByte(new_capacity);
        buff[5]=highByte(new_capacity);
        buff[6]=lowByte(new_capacity);
        buff[7]=highByte(new_capacity);
        buff[8]=lowByte(new_capacity);
        buff[9]=highByte(new_capacity);
        buff[10]=lowByte(new_capacity);
        buff[13]=0x00; // Сбрасываем флаг статуса обновления (Update Status) в 0x00.
        WriteSMBSubclass(82,0x78); // Записываем обновленные данные обратно в подкласс 82, страница 0x78.
        for (byte i=88; i<=95; i++) { // Цикл для обновления таблиц R_a (резисторов) для ячеек.
                                      // Эти значения являются типичными или "сырыми" данными для таблицы R_a.
          buff[0]=0x20; // Длина данных.
          buff[1]=0xFF; // Пример значений (зависит от конкретной химии батареи).
          buff[2]=(i<92) ? 0x55 : 0xFF; // Например, 0x55 для ячеек 0-3, 0xFF для остальных.
          buff[3]=0x00; buff[4]=0xA0; buff[5]=0x00; buff[6]=0xA6;
          buff[7]=0x00; buff[8]=0x99; buff[9]=0x00; buff[10]=0x97;
          buff[11]=0x00; buff[12]=0x91; buff[13]=0x00; buff[14]=0x98;
          buff[15]=0x00; buff[16]=0xB0; buff[17]=0x00; buff[18]=0xCC;
          buff[19]=0x00; buff[20]=0xDE; buff[21]=0x00; buff[22]=0xFE;
          buff[23]=0x01; buff[24]=0x3B; buff[25]=0x01; buff[26]=0xB5;
          buff[27]=0x02; buff[28]=0x8B; buff[29]=0x03; buff[30]=0xE9;
          buff[31]=0x05; buff[32]=0xB2;
          WriteSMBSubclass(i,0x78); // Записываем данные в подкласс i, страница 0x78.
        }
        break;
      case 0x39: // '9'
        WriteSMBWord(0x00,0x0021); // Команда для запуска алгоритма Impedance Track.
        Serial.println(F("Запуск алгоритма отслеживания импеданса..."));
        break;
    }
  } else Serial.read(); // Если есть данные в Serial, просто считываем их, чтобы очистить буфер.
}
