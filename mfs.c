// The MIT License (MIT)
// 
// Copyright (c) 2020 Trevor Bakker 
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#define _GNU_SOURCE

#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <stdint.h>

#define MAX_NUM_ARGUMENTS 3

#define WHITESPACE " \t\n"      // We want to split our command line up into tokens
                                // so we need to define what delimits our tokens.
                                // In this case  white space
                                // will separate the tokens on our command line

#define MAX_COMMAND_SIZE 255    // The maximum command-line size


// Struct holding the information of each entry in the FAT32 directory
struct __attribute__((__packed__)) DirectoryEntry
{
  char DIR_Name[11];
  uint8_t DIR_Attr;
  uint8_t Unused1[8];
  uint16_t DIR_FirstClusterHigh;
  uint8_t Unused2[4];
  uint16_t DIR_FirstClusterLow;
  uint32_t DIR_FileSize;
};

// Struct holding information about the FAT32 directory
struct f32info
{
  char BS_OEMName[8];
  int16_t BPB_BytsPerSec;
  int8_t BPB_SecPerClus;
  int16_t BPB_RsvdSecCnt;
  int8_t BPB_NumFATS;
  int16_t BPB_RootEntCnt;
  char BS_VolLab[11];
  int32_t BPB_FATSz32;
  int32_t BPB_RootClus;

  int32_t RootDirSectors = 0;
  int32_t FirstDataSector = 0;
  int32_t FirstSectorofCluster = 0;
};

/*
* Function    : LBAToOffset
* Parameters  : The current sector number that points to a block of data and struct of the fat32 directory information
* Returns     : The value of the address for that block of data
* Description : Finds the starting address of a block of data given the sector number
*               corresponding to that data block.
*/

int LBAToOffset(int32_t sector, struct f32info *f32)
{
  return (( sector - 2 ) * f32->BPB_BytsPerSec) + (f32->BPB_BytsPerSec * f32->BPB_RsvdSecCnt) + (f32->BPD_NumFATs * f32->BPB_FATSz32 * f32->BPB_BytsPerSec);
}

/*
* Function    : NextLB
* Parameters  : Logical block address and struct of the fat32 directory information
*/
int16_t NextLB( uint32_t sector, struct *f32info f32, FILE* fp )
{
  uint32_t FATAddress = ( f32->BPB_BytsPerSec * f32->BPB_RsvdSecCnt ) + ( sector * 4 );
  int16_t val;
  fseek( fp, FATAddress, SEEK_GET);
  fread( &val, 2, 1, fp );
  return val;
}

// opens and reads the specified fat32 image. if image not found, returns null
FILE* openFat32File(char *token, struct f32info *f32, struct DirectoryEntry *dir)
{
  if( token[1] == NULL )
  {
    print("Error: Filename not given.\n");
    return NULL;
  }

  FILE *fp = fopen(token[1], "r");

  if(!fp)
  {
    printf("Error: File system image not found.\n");
    return NULL;
  }

  // seek and read fat32 directory information as specified by fatspec pdf
  fseek( fp, 3, SEEK_SET );
  fread(&f32->BS_OEMName, 8, 1, fp);

  fseek( fp, 11, SEEK_SET );
  fread(&f32->BPB_BytsPerSec, 2, 1, fp);
  
  fseek( fp, 13, SEEK_SET );
  fread(&f32->BPB_SecPerClus, 1, 1, fp);

  fseek( fp, 14, SEEK_SET );
  fread(&f32->BPB_RsvdSecCnt, 2, 1, fp);

  fseek( fp, 16, SEEK_SET );
  fread(&f32->BPB_NumFATS, 1, 1, fp);

  fseek( fp, 17, SEEK_SET );
  fread(&f32->BPB_RootEntCnt, 2, 1, fp);

  fseek( fp, 71, SEEK_SET );
  fread(&f32->BS_VolLab, 11, 1, fp);

  fseek( fp, 36, SEEK_SET );
  fread(&f32->BPB_FATSz32, 4, 1, fp);

  fseek( fp, 44, SEEK_SET );
  fread(&f32->BPB_RootClus, 4, 1, fp);

  return fp;
}

int main()
{

  char * cmd_str = (char*) malloc( MAX_COMMAND_SIZE );
  
  FILE *fp;
  int file_is_open = 0;
  struct f32info *fat32 = ( struct f32info *)malloc( sizeof( struct DirectoryEntry ) * 16 );
  struct DirectoryEntry *dir = ( struct DirectoryEntry * )malloc( sizeof( struct DirectoryEntry ) * 16 ); // since fat32 can only have 16 represented

  while( 1 )
  {
    // Print out the mfs prompt
    printf ("mfs> ");

    // Read the command from the commandline.  The
    // maximum command that will be read is MAX_COMMAND_SIZE
    // This while command will wait here until the user
    // inputs something since fgets returns NULL when there
    // is no input
    while( !fgets (cmd_str, MAX_COMMAND_SIZE, stdin) );

    /* Parse input */
    char *token[MAX_NUM_ARGUMENTS];

    int   token_count = 0;                                 
                                                           
    // Pointer to point to the token
    // parsed by strsep
    char *arg_ptr;                                         
                                                           
    char *working_str  = strdup( cmd_str );                

    // we are going to move the working_str pointer so
    // keep track of its original value so we can deallocate
    // the correct amount at the end
    char *working_root = working_str;

    // Tokenize the input stringswith whitespace used as the delimiter
    while ( ( (arg_ptr = strsep(&working_str, WHITESPACE ) ) != NULL) && 
              (token_count<MAX_NUM_ARGUMENTS))
    {
      token[token_count] = strndup( arg_ptr, MAX_COMMAND_SIZE );
      if( strlen( token[token_count] ) == 0 )
      {
        token[token_count] = NULL;
      }
        token_count++;
    }

    // Now print the tokenized input as a debug check
    // \TODO Remove this code and replace with your FAT32 functionality

    // int token_index  = 0;
    // for( token_index = 0; token_index < token_count; token_index ++ ) 
    // {
    //   printf("token[%d] = %s\n", token_index, token[token_index] );  
    // }

    // maybe could use switch statement instead?

    // If the user types a blank line,
    // the program quietly prints another prompt and accepts a new line of input.
    if( token[0] == NULL );

    // opens a fat32 image.
    // filenames of fat32 images cannot not contain spaces and are limited to 100 characters.
    // if the file is not found or if a file system is already open, the program will
    // output the appropriate error.
    else if ( !strcmp( token[0], "open" ))
    {
      if ( !file_is_open )
      {
        fp = openFile(token, fat32, dir)
        if ( fp != NULL ) file_is_open = 1;
      }
      else
      {
        printf( "Error: File system image is already open.\n" );
      }
    }

    // closes a fat32 image.
    // if the file system is not currently open the program will give an error.
    // any command issued after a close except for open will prompt the user to
    // open a file system image first.
    else if ( !strcmp( token[0], "close" ))
    {

    }

    // prints out information about the file system in both hexadecimal and base 10.
    else if ( !strcmp( token[0], "info" ))
    {

    }

    // prints out the attributes and starting cluster number of the file/directory name.
    // if the parameter is a directory name then the size is 0.
    // if the file or directory does not exist then the program will output an error.
    else if ( !strcmp( token[0], "stat" ))
    {

    }

    // retrieves the file from the FAT32 image and places it in your current working directory.
    // if the file/directory does not exist then the program will output an error.
    else if ( !strcmp( token[0], "get" ))
    {

    }

    // changes the current working directory to the given directory.
    // supports both relative and absolute paths.
    else if ( !strcmp( token[0], "cd" ))
    {

    }

    // lists the directory contents.
    // skips deleted files and system volume names.
    else if ( !strcmp( token[0], "read" ))
    {

    }

    // deletes the file from the file system
    else if ( !strcmp( token[0], "del" ))
    {

    }

    // un-deletes the file from the file system 
    else if ( !strcmp( token[0], "undel" ))
    {

    }

    free( working_root );

  }

  // cleanup
  if( file_is_open ) fclose( fp );
  free( fat32 );
  free( dir );
  return 0;
}
