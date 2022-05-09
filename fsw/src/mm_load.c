/*************************************************************************
** File: mm_load.c 
**
**   Copyright � 2007-2014 United States Government as represented by the
**   Administrator of the National Aeronautics and Space Administration.
**   All Other Rights Reserved.
**
**   This software was created at NASA's Goddard Space Flight Center.
**   This software is governed by the NASA Open Source Agreement and may be
**   used, distributed and modified only pursuant to the terms of that
**   agreement.
**
** Purpose:
**   Provides functions for the execution of the CFS Memory Manager
**   load and fill ground commands
**
*************************************************************************/

/*************************************************************************
** Includes
*************************************************************************/
#include "mm_app.h"
#include "mm_load.h"
#include "mm_perfids.h"
#include "mm_events.h"
#include "mm_utils.h"
#include "mm_mem32.h"
#include "mm_mem16.h"
#include "mm_mem8.h"
#include "mm_mission_cfg.h"
#include "cfs_utils.h"
#include <string.h>

/*************************************************************************
** External Data
*************************************************************************/
extern MM_AppData_t MM_AppData;

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                 */
/* Memory poke ground command                                      */
/*                                                                 */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
bool MM_PokeCmd(const CFE_SB_Buffer_t *BufPtr)
{
    bool          Valid = false;
    cpuaddr       DestAddress;
    MM_PokeCmd_t *CmdPtr;
    uint16        ExpectedLength = sizeof(MM_PokeCmd_t);

    /* Verify command packet length */
    if (MM_VerifyCmdLength(&BufPtr->Msg, ExpectedLength))
    {
        CmdPtr = ((MM_PokeCmd_t *)BufPtr);

        /* Resolve the symbolic address in command message */
        Valid = CFS_ResolveSymAddr(&(CmdPtr->DestSymAddress), &DestAddress);

        if (Valid == true)
        {
            /* Run necessary checks on command parameters */
            Valid = MM_VerifyPeekPokeParams(DestAddress, CmdPtr->MemType, CmdPtr->DataSize);

            /* Check the specified memory type and call the appropriate routine */
            if (Valid == true)
            {
                /* Check if we need special EEPROM processing */
                if (CmdPtr->MemType == MM_EEPROM)
                {
                    MM_PokeEeprom(CmdPtr, DestAddress);
                }
                else
                {
                    /*
                    ** We can use this routine for all other memory types
                    *  (including the optional ones)
                    */
                    MM_PokeMem(CmdPtr, DestAddress);
                }

            } /* end MM_VerifyPeekPokeParams if */

        } /* end CFS_ResolveSymAddr */
        else
        {
            CFE_EVS_SendEvent(MM_SYMNAME_ERR_EID, CFE_EVS_EventType_ERROR,
                              "Symbolic address can't be resolved: Name = '%s'", CmdPtr->DestSymAddress.SymName);
        }

    } /* end MM_VerifyCmdLength if */

    return Valid;

} /* end MM_PokeCmd */

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                 */
/* Write 8, 16, or 32 bits of data to any RAM memory address       */
/*                                                                 */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
bool MM_PokeMem(const MM_PokeCmd_t *CmdPtr, cpuaddr DestAddress)
{
    uint8  ByteValue;
    uint16 WordValue;
    int32  PSP_Status     = CFE_PSP_SUCCESS;
    uint32 DataValue      = 0;
    uint32 BytesProcessed = 0;
    bool   ValidPoke      = false;
    uint8  DataSize       = 0; /* only used for giving MEM type/size in events */
    uint32 EventID        = 0;

    /* Write input number of bits to destination address */
    switch (CmdPtr->DataSize)
    {
        case MM_BYTE_BIT_WIDTH:
            ByteValue      = (uint8)CmdPtr->Data;
            DataValue      = (uint32)ByteValue;
            BytesProcessed = sizeof(uint8);
            DataSize       = 8;
            if ((PSP_Status = CFE_PSP_MemWrite8(DestAddress, ByteValue)) == CFE_PSP_SUCCESS)
            {
                EventID   = MM_POKE_BYTE_INF_EID;
                ValidPoke = true;
            }
            break;

        case MM_WORD_BIT_WIDTH:
            WordValue      = (uint16)CmdPtr->Data;
            DataValue      = (uint32)WordValue;
            BytesProcessed = sizeof(uint16);
            DataSize       = 16;
            if ((PSP_Status = CFE_PSP_MemWrite16(DestAddress, WordValue)) == CFE_PSP_SUCCESS)
            {
                EventID   = MM_POKE_WORD_INF_EID;
                ValidPoke = true;
            }
            break;

        case MM_DWORD_BIT_WIDTH:
            DataValue      = CmdPtr->Data;
            BytesProcessed = sizeof(uint32);
            DataSize       = 32;
            if ((PSP_Status = CFE_PSP_MemWrite32(DestAddress, DataValue)) == CFE_PSP_SUCCESS)
            {
                EventID   = MM_POKE_DWORD_INF_EID;
                ValidPoke = true;
            }
            break;

        /*
        ** We don't need a default case, a bad DataSize will get caught
        ** in the MM_VerifyPeekPokeParams function and we won't get here
        */
        default:
            break;
    }

    if (ValidPoke)
    {
        /* Update cmd counter and last action stats */
        MM_AppData.HkPacket.LastAction     = MM_POKE;
        MM_AppData.HkPacket.MemType        = CmdPtr->MemType;
        MM_AppData.HkPacket.Address        = DestAddress;
        MM_AppData.HkPacket.DataValue      = DataValue;
        MM_AppData.HkPacket.BytesProcessed = BytesProcessed;

        CFE_EVS_SendEvent(EventID, CFE_EVS_EventType_INFORMATION,
                          "Poke Command: Addr = 0x%08X, Size = %d bits, Data = 0x%08X", (unsigned int)DestAddress,
                          DataSize, (unsigned int)DataValue);
    }
    else
    {
        CFE_EVS_SendEvent(MM_PSP_WRITE_ERR_EID, CFE_EVS_EventType_ERROR,
                          "PSP write memory error: RC=0x%08X, Address=0x%08X, MemType=MEM%d", (unsigned int)PSP_Status,
                          (unsigned int)DestAddress, DataSize);
    }

    return ValidPoke;

} /* end MM_PokeMem */

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                 */
/* Write 8, 16, or 32 bits of data to any EEPROM memory address    */
/*                                                                 */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
bool MM_PokeEeprom(const MM_PokeCmd_t *CmdPtr, cpuaddr DestAddress)
{
    uint8  ByteValue;
    uint16 WordValue;
    int32  PSP_Status;
    uint32 DataValue      = 0;
    uint32 BytesProcessed = 0;
    bool   ValidPoke      = false;

    CFE_ES_PerfLogEntry(MM_EEPROM_POKE_PERF_ID);

    /* Write input number of bits to destination address */
    switch (CmdPtr->DataSize)
    {
        case MM_BYTE_BIT_WIDTH:
            ByteValue      = (uint8)CmdPtr->Data;
            DataValue      = (uint32)ByteValue;
            BytesProcessed = sizeof(uint8);
            PSP_Status     = CFE_PSP_EepromWrite8(DestAddress, ByteValue);
            if (PSP_Status != CFE_PSP_SUCCESS)
            {
                CFE_EVS_SendEvent(MM_OS_EEPROMWRITE8_ERR_EID, CFE_EVS_EventType_ERROR,
                                  "CFE_PSP_EepromWrite8 error received: RC = 0x%08X, Addr = 0x%08X",
                                  (unsigned int)PSP_Status, (unsigned int)DestAddress);
            }
            else
            {
                CFE_EVS_SendEvent(MM_POKE_BYTE_INF_EID, CFE_EVS_EventType_INFORMATION,
                                  "Poke Command: Addr = 0x%08X, Size = 8 bits, Data = 0x%02X",
                                  (unsigned int)DestAddress, ByteValue);
                ValidPoke = true;
            }
            break;

        case MM_WORD_BIT_WIDTH:
            WordValue      = (uint16)CmdPtr->Data;
            DataValue      = (uint32)WordValue;
            BytesProcessed = sizeof(uint16);
            PSP_Status     = CFE_PSP_EepromWrite16(DestAddress, WordValue);
            if (PSP_Status != CFE_PSP_SUCCESS)
            {
                CFE_EVS_SendEvent(MM_OS_EEPROMWRITE16_ERR_EID, CFE_EVS_EventType_ERROR,
                                  "CFE_PSP_EepromWrite16 error received: RC = 0x%08X, Addr = 0x%08X",
                                  (unsigned int)PSP_Status, (unsigned int)DestAddress);
            }
            else
            {
                CFE_EVS_SendEvent(MM_POKE_WORD_INF_EID, CFE_EVS_EventType_INFORMATION,
                                  "Poke Command: Addr = 0x%08X, Size = 16 bits, Data = 0x%04X",
                                  (unsigned int)DestAddress, WordValue);
                ValidPoke = true;
            }
            break;

        case MM_DWORD_BIT_WIDTH:
            DataValue      = CmdPtr->Data;
            BytesProcessed = sizeof(uint32);
            PSP_Status     = CFE_PSP_EepromWrite32(DestAddress, CmdPtr->Data);
            if (PSP_Status != CFE_PSP_SUCCESS)
            {
                CFE_EVS_SendEvent(MM_OS_EEPROMWRITE32_ERR_EID, CFE_EVS_EventType_ERROR,
                                  "CFE_PSP_EepromWrite32 error received: RC = 0x%08X, Addr = 0x%08X",
                                  (unsigned int)PSP_Status, (unsigned int)DestAddress);
            }
            else
            {
                CFE_EVS_SendEvent(MM_POKE_DWORD_INF_EID, CFE_EVS_EventType_INFORMATION,
                                  "Poke Command: Addr = 0x%08X, Size = 32 bits, Data = 0x%08X",
                                  (unsigned int)DestAddress, (unsigned int)(CmdPtr->Data));
                ValidPoke = true;
            }
            break;

        /*
        ** We don't need a default case, a bad DataSize will get caught
        ** in the MM_VerifyPeekPokeParams function and we won't get here
        */
        default:
            break;
    }

    if (ValidPoke)
    {
        /* Update cmd counter and last action stats */
        MM_AppData.HkPacket.LastAction     = MM_POKE;
        MM_AppData.HkPacket.MemType        = CmdPtr->MemType;
        MM_AppData.HkPacket.Address        = DestAddress;
        MM_AppData.HkPacket.DataValue      = DataValue;
        MM_AppData.HkPacket.BytesProcessed = BytesProcessed;
    }

    CFE_ES_PerfLogExit(MM_EEPROM_POKE_PERF_ID);

    return ValidPoke;

} /* end MM_PokeEeprom */

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                 */
/* Load memory with interrupts disabled                            */
/* Only valid for RAM addresses                                    */
/*                                                                 */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
bool MM_LoadMemWIDCmd(const CFE_SB_Buffer_t *BufPtr)
{
    MM_LoadMemWIDCmd_t *CmdPtr;
    uint32              ComputedCRC = 0;
    cpuaddr             DestAddress;
    uint16              ExpectedLength = sizeof(MM_LoadMemWIDCmd_t);
    bool                CmdResult      = false;

    /* Verify command packet length */
    if (MM_VerifyCmdLength(&BufPtr->Msg, ExpectedLength))
    {
        CmdPtr = ((MM_LoadMemWIDCmd_t *)BufPtr);

        /* Resolve the symbolic address in command message */
        if (CFS_ResolveSymAddr(&(CmdPtr->DestSymAddress), &DestAddress) == true)
        {
            /*
            ** Run some necessary checks on command parameters
            ** NOTE: A load with interrupts disabled command is only valid for RAM addresses
            */
            if (MM_VerifyLoadDumpParams(DestAddress, MM_RAM, CmdPtr->NumOfBytes, MM_VERIFY_WID) == true)
            {
                /* Verify data integrity check value */
                ComputedCRC = CFE_ES_CalculateCRC(CmdPtr->DataArray, CmdPtr->NumOfBytes, 0, MM_LOAD_WID_CRC_TYPE);
                /*
                ** If the CRC matches do the load
                */
                if (ComputedCRC == CmdPtr->Crc)
                {

                    /* Load input data to input memory address */
                    memcpy((void *)DestAddress, CmdPtr->DataArray, CmdPtr->NumOfBytes);

                    CmdResult = true;
                    CFE_EVS_SendEvent(MM_LOAD_WID_INF_EID, CFE_EVS_EventType_INFORMATION,
                                      "Load Memory WID Command: Wrote %d bytes to address: 0x%08X",
                                      (int)CmdPtr->NumOfBytes, (unsigned int)DestAddress);

                    /* Update last action statistics */
                    MM_AppData.HkPacket.LastAction     = MM_LOAD_WID;
                    MM_AppData.HkPacket.Address        = DestAddress;
                    MM_AppData.HkPacket.MemType        = MM_RAM;
                    MM_AppData.HkPacket.BytesProcessed = CmdPtr->NumOfBytes;
                }
                else
                {
                    CFE_EVS_SendEvent(MM_LOAD_WID_CRC_ERR_EID, CFE_EVS_EventType_ERROR,
                                      "Interrupts Disabled Load CRC failure: Expected = 0x%X Calculated = 0x%X",
                                      (unsigned int)CmdPtr->Crc, (unsigned int)ComputedCRC);
                }

            } /* end MM_VerifyLoadWIDParams */

        } /* end CFS_ResolveSymAddr if */
        else
        {
            CFE_EVS_SendEvent(MM_SYMNAME_ERR_EID, CFE_EVS_EventType_ERROR,
                              "Symbolic address can't be resolved: Name = '%s'", CmdPtr->DestSymAddress.SymName);
        }

    } /* end MM_VerifyCmdLength if */

    return CmdResult;

} /* end MM_LoadMemWIDCmd */

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                 */
/* Load memory from a file command                                 */
/*                                                                 */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
bool MM_LoadMemFromFileCmd(const CFE_SB_Buffer_t *BufPtr)
{
    bool                     Valid = false;
    osal_id_t                FileHandle;
    int32                    OS_Status = OS_SUCCESS;
    cpuaddr                  DestAddress;
    MM_LoadMemFromFileCmd_t *CmdPtr;
    CFE_FS_Header_t          CFEFileHeader;
    MM_LoadDumpFileHeader_t  MMFileHeader;
    uint32                   ComputedCRC;
    uint16                   ExpectedLength = sizeof(MM_LoadMemFromFileCmd_t);

    /* Verify command packet length */
    if (MM_VerifyCmdLength(&BufPtr->Msg, ExpectedLength))
    {
        CmdPtr = ((MM_LoadMemFromFileCmd_t *)BufPtr);

        /*
        ** NUL terminate the very end of the file name string array as a
        ** safety measure
        */
        CmdPtr->FileName[OS_MAX_PATH_LEN - 1] = '\0';

        /* Verify filename doesn't have any illegal characters */
        Valid = CFS_IsValidFilename(CmdPtr->FileName, strlen(CmdPtr->FileName));
        if (Valid == true)
        {
            /* Open load file for reading */
            OS_Status = OS_OpenCreate(&FileHandle, CmdPtr->FileName, OS_FILE_FLAG_NONE, OS_READ_ONLY);
            if (OS_Status == OS_SUCCESS)
            {
                /* Read in the file headers */
                Valid = MM_ReadFileHeaders(CmdPtr->FileName, FileHandle, &CFEFileHeader, &MMFileHeader);
                if (Valid == true)
                {
                    /* Verify the file size is correct */
                    Valid = MM_VerifyLoadFileSize(CmdPtr->FileName, &MMFileHeader);
                    if (Valid == true)
                    {
                        /* Verify data integrity check value */
                        OS_Status = CFS_ComputeCRCFromFile(FileHandle, &ComputedCRC, MM_LOAD_FILE_CRC_TYPE);
                        if (OS_Status == OS_SUCCESS)
                        {
                            /*
                            ** Reset the file pointer to the start of the load data, need to do this
                            ** because CFS_ComputeCRCFromFile reads to the end of file
                            */
                            OS_Status = OS_lseek(
                                FileHandle, (sizeof(CFE_FS_Header_t) + sizeof(MM_LoadDumpFileHeader_t)), OS_SEEK_SET);
                            if (OS_Status != (sizeof(CFE_FS_Header_t) + sizeof(MM_LoadDumpFileHeader_t)))
                            {
                                Valid = false;
                            }
                            /* Check the computed CRC against the file header CRC */
                            if ((ComputedCRC == MMFileHeader.Crc) && (Valid == true))
                            {
                                /* Resolve symbolic address in file header */
                                Valid = CFS_ResolveSymAddr(&(MMFileHeader.SymAddress), &DestAddress);

                                if (Valid == true)
                                {
                                    /* Run necessary checks on command parameters */
                                    Valid = MM_VerifyLoadDumpParams(DestAddress, MMFileHeader.MemType,
                                                                    MMFileHeader.NumOfBytes, MM_VERIFY_LOAD);
                                    if (Valid == true)
                                    {
                                        /* Call the load routine for the specified memory type */
                                        switch (MMFileHeader.MemType)
                                        {
                                            case MM_RAM:
                                            case MM_EEPROM:
                                                Valid = MM_LoadMemFromFile(FileHandle, CmdPtr->FileName, &MMFileHeader,
                                                                           DestAddress);
                                                break;

#ifdef MM_OPT_CODE_MEM32_MEMTYPE
                                            case MM_MEM32:
                                                Valid = MM_LoadMem32FromFile(FileHandle, CmdPtr->FileName,
                                                                             &MMFileHeader, DestAddress);
                                                break;
#endif /* MM_OPT_CODE_MEM32_MEMTYPE */

#ifdef MM_OPT_CODE_MEM16_MEMTYPE
                                            case MM_MEM16:
                                                Valid = MM_LoadMem16FromFile(FileHandle, CmdPtr->FileName,
                                                                             &MMFileHeader, DestAddress);
                                                break;
#endif /* MM_OPT_CODE_MEM16_MEMTYPE */

#ifdef MM_OPT_CODE_MEM8_MEMTYPE
                                            case MM_MEM8:
                                                Valid = MM_LoadMem8FromFile(FileHandle, CmdPtr->FileName, &MMFileHeader,
                                                                            DestAddress);
                                                break;
#endif /* MM_OPT_CODE_MEM8_MEMTYPE */

                                            /*
                                            ** We don't need a default case, a bad MemType will get caught
                                            ** in the MM_VerifyFileLoadParams function and we won't get here
                                            */
                                            default:
                                                Valid = false;
                                                break;
                                        }

                                        if (Valid == true)
                                        {
                                            CFE_EVS_SendEvent(MM_LD_MEM_FILE_INF_EID, CFE_EVS_EventType_INFORMATION,
                                                              "Load Memory From File Command: Loaded %d bytes to "
                                                              "address 0x%08X from file '%s'",
                                                              (int)MM_AppData.HkPacket.BytesProcessed,
                                                              (unsigned int)DestAddress, CmdPtr->FileName);
                                        }

                                    } /* end MM_VerifyFileLoadParams if */
                                    else
                                    {
                                        /*
                                        ** We don't need to increment the error counter here, it was done by the
                                        ** MM_VerifyFileLoadParams routine when the error was first discovered.
                                        ** We send this event as a supplemental message with the filename attached.
                                        */
                                        CFE_EVS_SendEvent(MM_FILE_LOAD_PARAMS_ERR_EID, CFE_EVS_EventType_ERROR,
                                                          "Load file failed parameters check: File = '%s'",
                                                          CmdPtr->FileName);
                                    }

                                } /* end CFS_ResolveSymAddr if */
                                else
                                {
                                    CFE_EVS_SendEvent(MM_SYMNAME_ERR_EID, CFE_EVS_EventType_ERROR,
                                                      "Symbolic address can't be resolved: Name = '%s'",
                                                      MMFileHeader.SymAddress.SymName);
                                }

                            } /* end ComputedCRC == MMFileHeader.Crc if */
                            else
                            {
                                Valid = false;
                                CFE_EVS_SendEvent(
                                    MM_LOAD_FILE_CRC_ERR_EID, CFE_EVS_EventType_ERROR,
                                    "Load file CRC failure: Expected = 0x%X Calculated = 0x%X File = '%s'",
                                    (unsigned int)MMFileHeader.Crc, (unsigned int)ComputedCRC, CmdPtr->FileName);
                            }

                        } /* end CFS_ComputeCRCFromFile if */
                        else
                        {
                            Valid = false;
                            CFE_EVS_SendEvent(MM_CFS_COMPUTECRCFROMFILE_ERR_EID, CFE_EVS_EventType_ERROR,
                                              "CFS_ComputeCRCFromFile error received: RC = 0x%08X File = '%s'",
                                              (unsigned int)OS_Status, CmdPtr->FileName);
                        }

                    } /* end MM_VerifyLoadFileSize */

                    /*
                    ** Don't need an 'else' here. MM_VerifyLoadFileSize will increment
                    ** the error counter and generate an event message if needed.
                    */

                } /* end MM_ReadFileHeaders if */

                /*
                ** Don't need an 'else' here. MM_ReadFileHeaders will increment
                ** the error counter and generate an event message if needed.
                */

                /* Close the load file for all cases after the open call succeeds */
                OS_Status = OS_close(FileHandle);
                if (OS_Status != OS_SUCCESS)
                {
                    Valid = false;
                    CFE_EVS_SendEvent(MM_OS_CLOSE_ERR_EID, CFE_EVS_EventType_ERROR,
                                      "OS_close error received: RC = 0x%08X File = '%s'", (unsigned int)OS_Status,
                                      CmdPtr->FileName);
                }

            } /* end OS_OpenCreate if */
            else
            {
                Valid = false;
                CFE_EVS_SendEvent(MM_OS_OPEN_ERR_EID, CFE_EVS_EventType_ERROR,
                                  "OS_OpenCreate error received: RC = %d File = '%s'", (int)OS_Status,
                                  CmdPtr->FileName);
            }

        } /* end IsValidFilename if */
        else
        {
            Valid = false;
            CFE_EVS_SendEvent(MM_CMD_FNAME_ERR_EID, CFE_EVS_EventType_ERROR,
                              "Command specified filename invalid: Name = '%s'", CmdPtr->FileName);
        }

    } /* end MM_VerifyCmdLength if */

    return Valid;

} /* end LoadMemFromFileCmd */

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                 */
/* Loads memory from a file                                        */
/*                                                                 */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
bool MM_LoadMemFromFile(uint32 FileHandle, const char *FileName, const MM_LoadDumpFileHeader_t *FileHeader,
                        cpuaddr DestAddress)
{
    bool   Valid          = false;
    int32  BytesRemaining = FileHeader->NumOfBytes;
    int32  BytesProcessed = 0;
    int32  ReadLength;
    uint32 SegmentSize   = MM_MAX_LOAD_DATA_SEG;
    uint8 *ioBuffer      = (uint8 *)&MM_AppData.LoadBuffer[0];
    uint8 *TargetPointer = (uint8 *)DestAddress;

    if (FileHeader->MemType == MM_EEPROM)
    {
        CFE_ES_PerfLogEntry(MM_EEPROM_FILELOAD_PERF_ID);
    }

    while (BytesRemaining != 0)
    {
        if (BytesRemaining < MM_MAX_LOAD_DATA_SEG)
        {
            SegmentSize = BytesRemaining;
        }

        if ((ReadLength = OS_read(FileHandle, ioBuffer, SegmentSize)) == SegmentSize)
        {
            memcpy(TargetPointer, ioBuffer, SegmentSize);

            BytesRemaining -= SegmentSize;
            BytesProcessed += SegmentSize;
            TargetPointer += SegmentSize;

            /* Prevent CPU hogging between load segments */
            if (BytesRemaining != 0)
            {
                MM_SegmentBreak();
            }
        }
        else
        {
            CFE_EVS_SendEvent(MM_OS_READ_ERR_EID, CFE_EVS_EventType_ERROR,
                              "OS_read error received: RC = 0x%08X Expected = %d File = '%s'", (unsigned int)ReadLength,
                              (int)SegmentSize, FileName);
            BytesRemaining = 0;
        }
    }

    if (FileHeader->MemType == MM_EEPROM)
    {
        CFE_ES_PerfLogExit(MM_EEPROM_FILELOAD_PERF_ID);
    }

    /* Update last action statistics */
    if (BytesProcessed == FileHeader->NumOfBytes)
    {
        Valid                              = true;
        MM_AppData.HkPacket.LastAction     = MM_LOAD_FROM_FILE;
        MM_AppData.HkPacket.MemType        = FileHeader->MemType;
        MM_AppData.HkPacket.Address        = DestAddress;
        MM_AppData.HkPacket.BytesProcessed = BytesProcessed;
        strncpy(MM_AppData.HkPacket.FileName, FileName, OS_MAX_PATH_LEN);
    }

    return (Valid);

} /* end MM_LoadMemFromFile */

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                 */
/* Verify load file size                                           */
/*                                                                 */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
bool MM_VerifyLoadFileSize(const char *FileName, const MM_LoadDumpFileHeader_t *FileHeader)
{

    bool       Valid = true;
    int32      OS_Status;
    uint32     ExpectedSize;
    int32      ActualSize; /* The size returned by OS_stat is signed */
    os_fstat_t FileStats;

    /*
    ** Get the filesystem statistics on our load file
    */
    OS_Status = OS_stat(FileName, &FileStats);
    if (OS_Status != OS_SUCCESS)
    {
        Valid = false;
        CFE_EVS_SendEvent(MM_OS_STAT_ERR_EID, CFE_EVS_EventType_ERROR,
                          "OS_stat error received: RC = 0x%08X File = '%s'", (unsigned int)OS_Status, FileName);
    }
    else
    {
        /*
        ** Check the reported size of the file against what it should be based
        ** upon the number of load bytes specified in the file header
        */
#ifdef OS_FILESTAT_SIZE
        ActualSize = OS_FILESTAT_SIZE(FileStats);
#else
        ActualSize = FileStats.st_size;
#endif
        ExpectedSize = FileHeader->NumOfBytes + sizeof(CFE_FS_Header_t) + sizeof(MM_LoadDumpFileHeader_t);
        if (ActualSize != ExpectedSize)
        {
            Valid = false;

            /*
            ** Note: passing FileStats.st_size in this event message will cause
            ** a segmentation fault under cygwin during unit testing, so we added
            ** the variable ActualSize to this function.
            */
            CFE_EVS_SendEvent(MM_LD_FILE_SIZE_ERR_EID, CFE_EVS_EventType_ERROR,
                              "Load file size error: Reported by OS = %d Expected = %d File = '%s'", (int)ActualSize,
                              (int)ExpectedSize, FileName);
        }
    }

    return (Valid);

} /* end MM_VerifyLoadFileSize */

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                 */
/* Read the cFE primary and and MM secondary file headers          */
/*                                                                 */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
bool MM_ReadFileHeaders(const char *FileName, int32 FileHandle, CFE_FS_Header_t *CFEHeader,
                        MM_LoadDumpFileHeader_t *MMHeader)
{
    bool  Valid = true;
    int32 OS_Status;

    /*
    ** Read in the primary cFE file header
    */
    OS_Status = CFE_FS_ReadHeader(CFEHeader, FileHandle);
    if (OS_Status != sizeof(CFE_FS_Header_t))
    {
        /* We either got an error or didn't read as much data as expected */
        Valid = false;
        CFE_EVS_SendEvent(MM_CFE_FS_READHDR_ERR_EID, CFE_EVS_EventType_ERROR,
                          "CFE_FS_ReadHeader error received: RC = 0x%08X Expected = %u File = '%s'",
                          (unsigned int)OS_Status, (unsigned int)sizeof(CFE_FS_Header_t), FileName);

    } /* end CFE_FS_ReadHeader if */
    else
    {
        /*
        ** Read in the secondary MM file header
        */
        OS_Status = OS_read(FileHandle, MMHeader, sizeof(MM_LoadDumpFileHeader_t));
        if (OS_Status != sizeof(MM_LoadDumpFileHeader_t))
        {
            /* We either got an error or didn't read as much data as expected */
            Valid = false;
            CFE_EVS_SendEvent(MM_OS_READ_EXP_ERR_EID, CFE_EVS_EventType_ERROR,
                              "OS_read error received: RC = 0x%08X Expected = %u File = '%s'", (unsigned int)OS_Status,
                              (unsigned int)sizeof(MM_LoadDumpFileHeader_t), FileName);

        } /* end OS_read if */

    } /* end CFE_FS_ReadHeader else */

    return (Valid);

} /* end MM_ReadFileHeaders */

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                 */
/* Fill memory command                                             */
/*                                                                 */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
bool MM_FillMemCmd(const CFE_SB_Buffer_t *BufPtr)
{
    cpuaddr          DestAddress;
    MM_FillMemCmd_t *CmdPtr         = (MM_FillMemCmd_t *)BufPtr;
    uint16           ExpectedLength = sizeof(MM_FillMemCmd_t);
    bool             CmdResult      = false;

    /* Verify command packet length */
    if (MM_VerifyCmdLength(&BufPtr->Msg, ExpectedLength))
    {
        /* Resolve symbolic address */
        if (CFS_ResolveSymAddr(&(CmdPtr->DestSymAddress), &DestAddress) == true)
        {
            /* Run necessary checks on command parameters */
            if (MM_VerifyLoadDumpParams(DestAddress, CmdPtr->MemType, CmdPtr->NumOfBytes, MM_VERIFY_FILL) == true)
            {
                switch (CmdPtr->MemType)
                {
                    case MM_RAM:
                    case MM_EEPROM:
                        CmdResult = MM_FillMem(DestAddress, CmdPtr);
                        break;

#ifdef MM_OPT_CODE_MEM32_MEMTYPE
                    case MM_MEM32:
                        CmdResult = MM_FillMem32(DestAddress, CmdPtr);
                        break;
#endif

#ifdef MM_OPT_CODE_MEM16_MEMTYPE
                    case MM_MEM16:
                        CmdResult = MM_FillMem16(DestAddress, CmdPtr);
                        break;
#endif

#ifdef MM_OPT_CODE_MEM8_MEMTYPE
                    case MM_MEM8:
                        CmdResult = MM_FillMem8(DestAddress, CmdPtr);
                        break;
#endif

                    /*
                    ** We don't need a default case, a bad MemType will get caught
                    ** in the MM_VerifyLoadDumpParams function and we won't get here
                    */
                    default:
                        CmdResult = false;
                        break;
                }

                if (MM_AppData.HkPacket.LastAction == MM_FILL)
                {
                    CFE_EVS_SendEvent(MM_FILL_INF_EID, CFE_EVS_EventType_INFORMATION,
                                      "Fill Memory Command: Filled %d bytes at address: 0x%08X with pattern: 0x%08X",
                                      (int)MM_AppData.HkPacket.BytesProcessed, (unsigned int)DestAddress,
                                      (unsigned int)MM_AppData.HkPacket.DataValue);
                }
            }
        }
        else
        {
            CFE_EVS_SendEvent(MM_SYMNAME_ERR_EID, CFE_EVS_EventType_ERROR,
                              "Symbolic address can't be resolved: Name = '%s'", CmdPtr->DestSymAddress.SymName);
        }
    }

    return CmdResult;

} /* end MM_FillMemCmd */

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                 */
/* Fill memory with the command specified fill pattern             */
/*                                                                 */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
bool MM_FillMem(cpuaddr DestAddress, const MM_FillMemCmd_t *CmdPtr)
{
    uint16 i;
    bool   Valid          = true;
    uint32 BytesProcessed = 0;
    uint32 BytesRemaining = CmdPtr->NumOfBytes;
    uint32 SegmentSize    = MM_MAX_FILL_DATA_SEG;
    uint8 *TargetPointer  = (uint8 *)DestAddress;
    uint8 *FillBuffer     = (uint8 *)&MM_AppData.FillBuffer[0];

    /* Create a scratch buffer with one fill segment */
    for (i = 0; i < (MM_MAX_FILL_DATA_SEG / sizeof(uint32)); i++)
    {
        FillBuffer[i] = CmdPtr->FillPattern;
    }

    /* Start EEPROM performance monitor */
    if (CmdPtr->MemType == MM_EEPROM)
    {
        CFE_ES_PerfLogEntry(MM_EEPROM_FILL_PERF_ID);
    }

    /* Fill memory one segment at a time */
    while (BytesRemaining != 0)
    {
        /* Last fill segment may be partial size */
        if (BytesRemaining < MM_MAX_FILL_DATA_SEG)
        {
            SegmentSize = BytesRemaining;
        }

        memcpy(TargetPointer, FillBuffer, SegmentSize);

        TargetPointer += SegmentSize;
        BytesProcessed += SegmentSize;
        BytesRemaining -= SegmentSize;
    }

    /* Stop EEPROM performance monitor */
    if (CmdPtr->MemType == MM_EEPROM)
    {
        CFE_ES_PerfLogExit(MM_EEPROM_FILL_PERF_ID);
    }

    /* Update last action statistics */
    MM_AppData.HkPacket.LastAction     = MM_FILL;
    MM_AppData.HkPacket.MemType        = CmdPtr->MemType;
    MM_AppData.HkPacket.Address        = DestAddress;
    MM_AppData.HkPacket.DataValue      = CmdPtr->FillPattern;
    MM_AppData.HkPacket.BytesProcessed = BytesProcessed;

    return Valid;

} /* End MM_FillMem */

/************************/
/*  End of File Comment */
/************************/