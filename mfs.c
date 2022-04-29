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
// #include <sys/wait.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MAX_NUM_ARGUMENTS 5

#define WHITESPACE " \t\n" // We want to split our command line up into tokens
                           // so we need to define what delimits our tokens.
                           // In this case  white space
                           // will separate the tokens on our command line

#define MAX_COMMAND_SIZE 255 // The maximum command-line size

// Struct holding the information of each entry in the FAT32 directory
struct __attribute__( ( __packed__ ) ) DirectoryEntry
{
    char DIR_Name[ 11 ];
    uint8_t DIR_Attr;
    uint8_t Unused1[ 8 ];
    uint16_t DIR_FirstClusterHigh;
    uint8_t Unused2[ 4 ];
    uint16_t DIR_FirstClusterLow;
    uint32_t DIR_FileSize;
};

// Struct holding information about the FAT32 directory
struct f32info
{
    char BS_OEMName[ 8 ];
    int16_t BPB_BytsPerSec;
    int8_t BPB_SecPerClus;
    int16_t BPB_RsvdSecCnt;
    int8_t BPB_NumFATS;
    int16_t BPB_RootEntCnt;
    char BS_VolLab[ 11 ];
    int32_t BPB_FATSz32;
    int32_t BPB_RootClus;
    char originalFilenames[ 16 ][ 11 ];

    int32_t RootDirSectors;
    int32_t FirstDataSector;
    int32_t FirstSectorofCluster;
};

/*
 * Function    : LBAToOffset
 * Parameters  : The current sector number that points to a block of data and struct of the fat32 directory information
 * Returns     : The value of the address for that block of data
 * Description : Finds the starting address of a block of data given the sector number
 *               corresponding to that data block.
 */

int LBAToOffset( int32_t sector, struct f32info *f32 )
{
    return ( ( sector - 2 ) * f32->BPB_BytsPerSec ) + ( f32->BPB_BytsPerSec * f32->BPB_RsvdSecCnt ) + ( f32->BPB_NumFATS * f32->BPB_FATSz32 * f32->BPB_BytsPerSec );
}

/*
 * Function    : NextLB
 * Parameters  : Logical block address and struct of the fat32 directory information
 */
int16_t NextLB( uint32_t sector, struct f32info *f32, FILE *fp )
{
    uint32_t FATAddress = ( f32->BPB_BytsPerSec * f32->BPB_RsvdSecCnt ) + ( sector * 4 );
    int16_t val;
    fseek( fp, FATAddress, SEEK_SET );
    fread( &val, 2, 1, fp );
    return val;
}

/*
 * Function    : openFat32File
 * Parameters  : Fat32 image file, fat32 info structure, and directory array
 * Returns     : On success, the file pointer to the fat32 image. On failure, returns null
 * Description : Opens and reads the specified fat32 image.
 */
FILE *openFat32File( char *filename, struct f32info *f32, struct DirectoryEntry *dir )
{

    FILE *fp = fopen( filename, "r+" );

    if ( !fp )
    {
        printf( "Error: File system image not found.\n" );
        return NULL;
    }

    // seek and read fat32 directory information as specified by fatspec pdf
    fseek( fp, 3, SEEK_SET );
    fread( &f32->BS_OEMName, 8, 1, fp );

    fseek( fp, 11, SEEK_SET );
    fread( &f32->BPB_BytsPerSec, 2, 1, fp );

    fseek( fp, 13, SEEK_SET );
    fread( &f32->BPB_SecPerClus, 1, 1, fp );

    fseek( fp, 14, SEEK_SET );
    fread( &f32->BPB_RsvdSecCnt, 2, 1, fp );

    fseek( fp, 16, SEEK_SET );
    fread( &f32->BPB_NumFATS, 1, 1, fp );

    fseek( fp, 17, SEEK_SET );
    fread( &f32->BPB_RootEntCnt, 2, 1, fp );

    fseek( fp, 71, SEEK_SET );
    fread( &f32->BS_VolLab, 11, 1, fp );

    fseek( fp, 36, SEEK_SET );
    fread( &f32->BPB_FATSz32, 4, 1, fp );

    fseek( fp, 44, SEEK_SET );
    fread( &f32->BPB_RootClus, 4, 1, fp );

    f32->RootDirSectors = 0;
    f32->FirstDataSector = 0;
    f32->FirstSectorofCluster = 0;

    int rootOffset = LBAToOffset( f32->BPB_RootClus, f32 );

    fseek( fp, rootOffset, SEEK_SET );
    fread( &dir[ 0 ], 32, 16, fp ); // root directory contains 16 32-byte records

    int i, j;
    for ( i = 0; i < 16; i++ )
    {
        strncpy( f32->originalFilenames[ i ], dir[ 0 ].DIR_Name, 11 );
    }
    return fp;
}

/*
 * Function    : printFat32Info
 * Parameters  : Fat32 file system info
 * Description : Prints out information about the file system in both hexadecimal and base 10
 */
void printFat32Info( struct f32info *f32 )
{
    printf( "--BPB_BytsPerSec:      hex: %-#10x  base10: %d\n", f32->BPB_BytsPerSec, f32->BPB_BytsPerSec );
    printf( "--BPB_SecPerClus:      hex: %-#10x  base10: %d\n", f32->BPB_SecPerClus, f32->BPB_SecPerClus );
    printf( "--BPB_RsvdSecCnt:      hex: %-#10x  base10: %d\n", f32->BPB_RsvdSecCnt, f32->BPB_RsvdSecCnt );
    printf( "--BPB_NumFATS:         hex: %-#10x  base10: %d\n", f32->BPB_NumFATS, f32->BPB_NumFATS );
    printf( "--BPB_FATSz32:         hex: %-#10x  base10: %d\n", f32->BPB_FATSz32, f32->BPB_FATSz32 );
}

/*
 * Function    : compare_filename
 * Parameters  : User filename input and directory entry filename
 * Returns     : Returns 1 if matches (success), 0 if doesn't match (failure)
 * Description : Checks if the user filename matches the directory entry filename
 */
int compare_filename( char *input, char *IMG_Name )
{
    char input_name[ 12 ];
    memset( input_name, '\0', 12 );
    strncpy( input_name, input, strlen( input ) );

    if ( strncmp( input_name, "..", 2 ) == 0 ) // User input ".."
    {
        if ( strncmp( input_name, IMG_Name, 2 ) == 0 ) // Match found
        {
            return 1;
        }
        else
        {
            return 0; // No match found
        }
    }

    char expanded_name[ 12 ];
    memset( expanded_name, ' ', 12 );

    char *token = strtok( input_name, "." ); // input copied to input_name, so that input isn't overwritten by strtok()

    strncpy( expanded_name, token, strlen( token ) ); // File name (token) copied over, limited to 11 characters

    token = strtok( NULL, "." );

    if ( token )
    {
        strncpy( ( char * )( expanded_name + 8 ), token, 3 ); // File extension, 3 characters, 3 bytes
    }

    expanded_name[ 11 ] = '\0';

    int i;
    for ( i = 0; i < 11; i++ )
    {
        expanded_name[ i ] = toupper( expanded_name[ i ] );
    }

    int result = strncmp( expanded_name, IMG_Name, 11 );
    if ( result == 0 ) // Match found
    {
        return 1;
    }
    else // No match found
    {
        return 0;
    }
}

/*
 * Function    : find_file
 * Parameters  : User filename input and directory entry array
 * Returns     : Returns index of file if found (success), -1 if not found (failure)
 * Description : Looks for file from a given directory (directory entry array)
 */
int find_file( char *filename, struct DirectoryEntry *dir )
{
    int i;

    // Search directory for entry
    for ( i = 0; i < 16; i++ )
    {
        if ( compare_filename( filename, dir[ i ].DIR_Name ) ) // 1 = true, name matches. 0 = false, no match
        {
            return i;
        }
    }

    // File was not found
    return -1;
}

/*
 * Function    : stat
 * Parameters  : User filename input and directory entry array
 * Description : Prints attributes of selected file / directory
 */
void stat( char *filename, struct DirectoryEntry *dir )
{
    int entry;
    char name_buffer[ 12 ];

    entry = find_file( filename, dir );

    // File not found
    if ( entry == -1 )
    {
        printf( "Error: File not found. \n" );
    }
    // File found
    else
    {
        strncpy( name_buffer, dir[ entry ].DIR_Name, 11 ); // Copy 11 characters from DIR_Name (total size is 11 bytes) to name buffer
        name_buffer[ 11 ] = '\0';                          // Manually add null character in index 12

        printf( "Name:               %s \n", name_buffer );
        printf( "Attribute:          %#x\n", dir[ entry ].DIR_Attr );
        printf( "FirstClusterHigh:   %u \n", dir[ entry ].DIR_FirstClusterHigh );
        printf( "FirstClusterLow:    %u \n", dir[ entry ].DIR_FirstClusterLow );
        printf( "FileSize:           %u \n", dir[ entry ].DIR_FileSize );
    }
}

/*
 * Function    : ls
 * Parameters  : Directory entry array
 * Description : Lists all files and sub-directories contained in current directory
 */
void ls( struct DirectoryEntry *dir )
{
    int i;
    uint8_t first_byte;

    // As DIR_Name does not terminate with a '\0' null character,
    // it needs to be added manually
    char name_buffer[ 12 ];
    char entry_attr;

    for ( i = 0; i < 16; i++ )
    {
        // Skip if directory entry is not a read-only file, a sub-directory, or an archive
        entry_attr = dir[ i ].DIR_Attr;
        if ( entry_attr != 0x01 && entry_attr != 0x10 && entry_attr != 0x20 ) continue;

        strncpy( name_buffer, dir[ i ].DIR_Name, 11 ); // Copy 11 characters from DIR_Name (total size is 11 bytes) to name buffer
        name_buffer[ 11 ] = '\0';                      // Manually add null character in index 12

        // Skip if directory entry is deleted
        first_byte = name_buffer[ 0 ];
        if ( first_byte == 0xe5 ) continue;

        printf( "%s \n", name_buffer ); // Print name buffer
    }
}

/*
 * Function    : cd
 * Parameters  : User filename input, directory, fat32info, and the current file pointer
 * Description : Changes the currect working directory to the given directory
 */
void cd( char *filename, struct DirectoryEntry *dir, struct f32info *f32, FILE *fp )
{
    int entry;
    char entry_attr;

    entry = find_file( filename, dir );

    // File not found
    if ( entry == -1 )
    {
        printf( "Error: File not found. \n" );
    }
    // File found
    else
    {
        // Fails if selected entry is not a directory
        entry_attr = dir[ entry ].DIR_Attr;
        if ( entry_attr != 0x10 )
        {
            printf( "Error: Entry is not a directory. \n" );
            return;
        }

        int cluster = dir[ entry ].DIR_FirstClusterLow;

        if ( cluster == 0 ) // Going to root
        {
            cluster = f32->BPB_RootClus;
        }
        int offset = LBAToOffset( cluster, f32 );

        fseek( fp, offset, SEEK_SET );
        fread( &dir[ 0 ], 32, 16, fp );
    }
}

/*
 * Function    : del
 * Parameters  : User filename input, directory, fat32info, and the current file pointer
 * Description : Deletes a file from the directory
 */
void del( char *filename, struct DirectoryEntry *dir, struct f32info *f32, FILE *fp )
{
    int entry;
    char entry_attr;

    entry = find_file( filename, dir );

    // File not found
    if ( entry == -1 )
    {
        printf( "Error: File not found. \n" );
    }
    // File found
    else
    {
        dir[ entry ].DIR_Name[ 0 ] = 0xe5;

        // int offset = LBAToOffset( dir[i].DIR_FirstClusterLow, f32 ); //dir[i].DIR_FirstClusterLow
        // fseek( fp, offset, SEEK_SET );
        // fwrite( &( dir[i].DIR_Name[0] ), 1, 1, fp );
        int rootOffset = LBAToOffset( f32->BPB_RootClus, f32 );
        fseek( fp, rootOffset, SEEK_SET );
        fwrite( &dir[ 0 ], 32, 16, fp ); // update image file
    }
}

/*
 * Function    : undel
 * Parameters  : User filename input, directory, fat32info, and the current file pointer
 * Description : Undeletes a file from the directory
 */
void undel( char *filename, struct DirectoryEntry *dir, struct f32info *f32, FILE *fp )
{
    int i;
    int file_not_found = 1;
    char entry_attr;

    // Search directory for entry
    for ( i = 0; i < 16; i++ )
    {
        entry_attr = dir[ i ].DIR_Attr;
        if ( entry_attr != 0x01 && entry_attr != 0x10 && entry_attr != 0x20 ) continue;

        if ( compare_filename( filename, f32->originalFilenames[ i ] ) ) // Found name match
        {
            file_not_found = 0;
            dir[ i ].DIR_Name[ 0 ] = f32->originalFilenames[ i ][ 0 ];

            // int offset = LBAToOffset( dir[i].DIR_FirstClusterLow, f32 ); //dir[i].DIR_FirstClusterLow
            // fseek( fp, offset, SEEK_SET );
            // fwrite( &( dir[i].DIR_Name[0] ), 1, 1, fp );
            int rootOffset = LBAToOffset( f32->BPB_RootClus, f32 );
            fseek( fp, rootOffset, SEEK_SET );
            fwrite( &dir[ 0 ], 32, 16, fp ); // update image file
        }
    }

    // File was not found
    if ( file_not_found )
    {
        printf( "Error: File not found. \n" );
    }
}

/*
 * Function    : get
 * Parameters  : User filename input, directory, fat32info, and the current file pointer
 * Description : Retrieves a file from the fat32 image and places it inside the current working directory
 */
void get( char *filename, struct DirectoryEntry *dir, struct f32info *f32, FILE *fp )
{
    int entry;
    int working_size;
    int sector_size = f32->BPB_BytsPerSec;

    entry = find_file( filename, dir );

    // File not found
    if ( entry == -1 )
    {
        printf( "Error: File not found. \n" );
    }
    // File found
    else
    {
        working_size = dir[ entry ].DIR_FileSize;
        FILE *localfp = fopen( filename, "w" );

        int nextSector = dir[ entry ].DIR_FirstClusterLow;
        uint8_t data[ sector_size ];

        while ( working_size > sector_size )
        {
            // printf("working_size = %d\n", working_size);
            fseek( fp, LBAToOffset( nextSector, f32 ), SEEK_SET );
            fread( &data, sector_size, 1, fp );
            fwrite( &data, sector_size, 1, localfp );
            working_size -= sector_size;
            nextSector = NextLB( nextSector, f32, fp );
        }

        fseek( fp, LBAToOffset( nextSector, f32 ), SEEK_SET );
        fread( &data, working_size, 1, fp );
        fwrite( &data, working_size, 1, localfp );

        fclose( localfp );
    }
}

/*
 * Function    : read_file
 * Parameters  : Filename, position(bytes), number of bytes to read, directory, fat32info, and file pointer
 * Description : Reads the the file at the specified position and outputs the number of bytes specified
 */
void read_file( char *filename, char *position, char *num_bytes, struct DirectoryEntry *dir, struct f32info *f32, FILE *fp )
{
    int entry;
    int working_size = atoi( num_bytes );
    int sector_size = f32->BPB_BytsPerSec;
    int position_int = atoi( position ); // convert arguments to correct type

    entry = find_file( filename, dir );

    // File not found
    if ( entry == -1 )
    {
        printf( "Error: File not found. \n" );
    }
    // File found
    else
    {
        int nextSector = dir[ entry ].DIR_FirstClusterLow;
        uint8_t data;

        while ( position_int >= sector_size ) // if position is larger than or equal to sector size, need to go to next sector
        {
            position_int -= sector_size;
            nextSector = NextLB( nextSector, f32, fp );
        }

        fseek( fp, LBAToOffset( nextSector, f32 ) + position_int, SEEK_SET );

        int i, sector_index = 0;
        for ( i = 0; i < working_size; i++ ) // print out every byte in hexadecimal
        {
            if ( position_int + sector_index == sector_size ) // if reach end of sector, go to next sector
            {
                position_int = 0;
                sector_index = 0;
                nextSector = NextLB( nextSector, f32, fp );
                fseek( fp, LBAToOffset( nextSector, f32 ), SEEK_SET );
            }
            fread( &data, 1, 1, fp );
            // printf( "%x ", data );
            printf( "%c", data );
            sector_index++;
        }
        printf( "\n" );
    }
}

int main()
{

    char *cmd_str = ( char * )malloc( MAX_COMMAND_SIZE );

    FILE *fp = NULL;
    struct f32info *fat32 = ( struct f32info * )malloc( sizeof( struct f32info ) );
    struct DirectoryEntry *dir = ( struct DirectoryEntry * )malloc( sizeof( struct DirectoryEntry ) * 16 ); // since fat32 can only have 16 represented

    while ( 1 )
    {
        // Print out the mfs prompt
        printf( "mfs> " );

        // Read the command from the commandline.  The
        // maximum command that will be read is MAX_COMMAND_SIZE
        // This while command will wait here until the user
        // inputs something since fgets returns NULL when there
        // is no input
        while ( !fgets( cmd_str, MAX_COMMAND_SIZE, stdin ) )
            ;

        /* Parse input */
        char *token[ MAX_NUM_ARGUMENTS ];

        int token_count = 0;

        // Pointer to point to the token
        // parsed by strsep
        char *arg_ptr;

        char *working_str = strdup( cmd_str );

        // we are going to move the working_str pointer so
        // keep track of its original value so we can deallocate
        // the correct amount at the end
        char *working_root = working_str;

        // Tokenize the input stringswith whitespace used as the delimiter
        while ( ( ( arg_ptr = strsep( &working_str, WHITESPACE ) ) != NULL ) &&
                ( token_count < MAX_NUM_ARGUMENTS ) )
        {
            token[ token_count ] = strndup( arg_ptr, MAX_COMMAND_SIZE );
            if ( strlen( token[ token_count ] ) == 0 )
            {
                token[ token_count ] = NULL;
            }
            token_count++;
        }

        // If the user types a blank line,
        // the program quietly prints another prompt and accepts a new line of input.
        if ( token[ 0 ] == NULL )
            ;

        // opens a fat32 image.
        // filenames of fat32 images cannot not contain spaces and are limited to 100 characters.
        // if the file is not found or if a file system is already open, the program will
        // output the appropriate error.
        else if ( !strcmp( token[ 0 ], "open" ) )
        {
            if ( fp == NULL )
            {
                if ( token[ 1 ] == NULL ) printf( "Error: Filename not given.\n" );
                else fp = openFat32File( token[ 1 ], fat32, dir );
            }

            else printf( "Error: File system image is already open.\n" );
        }

        // closes a fat32 image.
        // if the file system is not currently open the program will give an error.
        // any command issued after a close except for open will prompt the user to
        // open a file system image first.
        else if ( !strcmp( token[ 0 ], "close" ) )
        {
            if ( fp != NULL )
            {
                fclose( fp );
                fp = NULL;
            }
            else
            {
                printf( "Error: File system not open.\n" );
            }
        }

        // if the user types quit or exit, clean up and terminate program.
        else if ( ( strcmp( token[ 0 ], "quit" ) == 0 ) || ( strcmp( token[ 0 ], "exit" ) == 0 ) )
        {
            // cleanup
            free( working_root );
            if ( fp != NULL ) fclose( fp );
            free( fat32 );
            free( dir );
            exit( 0 );
        }

        // if a file is not open, any command issued will print out an error.
        else if ( fp == NULL ) printf( "Error: File system image must be opened first.\n" );

        // prints out information about the file system in both hexadecimal and base 10.
        else if ( !strcmp( token[ 0 ], "info" ) ) printFat32Info( fat32 );

        // prints out the attributes and starting cluster number of the file/directory name.
        // if the parameter is a directory name then the size is 0.
        // if the file or directory does not exist then the program will output an error.
        else if ( !strcmp( token[ 0 ], "stat" ) )
        {
            if ( token[ 1 ] == NULL ) printf( "Error: Filename not given.\n" );
            else stat( token[ 1 ], dir );
        }

        // retrieves the file from the FAT32 image and places it in your current working directory.
        // if the file/directory does not exist then the program will output an error.
        else if ( !strcmp( token[ 0 ], "get" ) )
        {
            if ( token[ 1 ] == NULL ) printf( "Error: Filename not given.\n" );
            else get( token[ 1 ], dir, fat32, fp );
        }

        // changes the current working directory to the given directory.
        // supports both relative and absolute paths.
        else if ( !strcmp( token[ 0 ], "cd" ) )
        {
            if ( token[ 1 ] == NULL ) printf( "Error: Filename not given.\n" );
            else cd( token[ 1 ], dir, fat32, fp );
        }

        // Lists the directory contents. Your program shall support listing “.” and “..” . Your program shall
        // not list deleted files or system volume names.
        else if ( !strcmp( token[ 0 ], "ls" ) )
        {
            ls( dir );
        }

        // Reads from the given file at the position, in bytes, specified by the parameter,
        // and outputs the number of bytes specified
        else if ( !strcmp( token[ 0 ], "read" ) )
        {
            if ( token_count - 1 < 4 ) printf( "Error: Not enough arguments. (%d arguments given)\n", token_count );
            else read_file( token[ 1 ], token[ 2 ], token[ 3 ], dir, fat32, fp );
        }

        // deletes the file from the file system
        else if ( !strcmp( token[ 0 ], "del" ) )
        {
            if ( token[ 1 ] == NULL ) printf( "Error: Filename not given.\n" );
            else del( token[ 1 ], dir, fat32, fp );
        }

        // un-deletes the file from the file system
        else if ( !strcmp( token[ 0 ], "undel" ) )
        {
            if ( token[ 1 ] == NULL ) printf( "Error: Filename not given.\n" );
            else undel( token[ 1 ], dir, fat32, fp );
        }

        else printf( "Error: Unknown command.\n" );

        free( working_root );
    }

    return 0;
}
