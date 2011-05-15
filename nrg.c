/*
 * This file is part of nerorip. (c)2011 Joe Balough
 *
 * Nerorip is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Nerorip is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with nerorip.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "nrg.h"

/**
 * Process the next chunk of data starting from the file's current position.
 */
void process_next_chunk(FILE *input_image) {
  // Chunk data came from the source for libdiscmage at http://www.koders.com/c/fidCFCF40C7DBB1F855208886B92C3C77ED6DBE45F3.aspx
  // The chunk ID and chunk size are always 32 bit integers
  const long int start_offset = ftell(input_image);
  const uint32_t chunk_id = fread32u(input_image);
  const uint32_t chunk_size = fread32u(input_image);

  if (chunk_id == CUES || chunk_id == CUEX) {
    /**
     * CUES / CUEX (Cue Sheet) Format:
     *   Indicates a disc at once image
     *   Chunk size = (# tracks + 1) * 16
     *
     * 1 B     Mode                         (0x41 = mode2, 0x01 = audio)
     * 1 B     Track number                 0x00
     * 1 B     Index                        0x00
     * 1 B     00
     * 4 B     V1: MM:SS:FF=0; V2: StartLBA ((LBA for S1T1 is 0xffffff6a) (MSF = 00:00:00))
     * ---
     * 1 B     Mode
     * 1 B     Track number                 (First is 1, increments over tracks in ALL sessions)
     * 1 B     Index                        (0x00, which is pregap for track)
     * 1 B     00
     * 4 B     V1: MMSSFF; V2: LBA          (MMSSFF = index) (S1T1's LBA is 0xffffff6a)
     * 1 B     Mode
     * 1 B     Track number                 (First is 1, increments over tracks in ALL sessions)
     * 1 B     Index                        (0x01, which is main track)
     * 1 B     00
     * 4 B     V1: MMSSFF; V2: LBA          (MMSSFF = index) (LBA is where track actually starts)
     * ... Repeat for each track in session
     * ---
     * 4 B    mm AA 01 00   mm = mode (if version 2)
     * 4 B    V1: Last MMSSFF; V2: Last LBA (MMSSFF == index1 + length)
     *
     * About the LBA: The first LBA is the starting LBA for this session. If it's the first session, it's always 0xffffff6a.
     *                The middle part repeats once for each session. The first LBA is the pre-start LBA for the track and the
     *                second is indicates where the track actually begins.
     *                Unless the track is audio with an intro bit (where the player starts at a negative time), the second LBA
     *                in each loop is 0x00000000
     */
    int number_tracks = chunk_size / 16 - 1;
    static int session_number = 1;
    static int track_number = 1;

    uint8_t session_mode = fread8u(input_image);

    // Skip junk
    assert(fread8u(input_image) == 0x00); // Track
    assert(fread8u(input_image) == 0x00); // 0x00
    assert(fread8u(input_image) == 0x00); // Index

    uint32_t session_start_LBA = fread32u(input_image);
    printf("%s at 0x%X:\tSize - %d B, Session %d has %d tracks using mode %s and starting at 0x%X.\n", (chunk_id == CUES ? "CUES" : "CUEX"), start_offset, chunk_size, session_number, number_tracks, (session_mode == 0x41 ? "Mode2" : (session_mode == 0x01 ? "Audio" : "Unknown")), session_start_LBA);

    int i = 1;
    for (i = 1; i <= number_tracks; i++, track_number++) {
      uint8_t pretrack_mode  = fread8u(input_image);
      assert(fread8u(input_image) == track_number);    // Track number
      assert(fread8u(input_image) == 0x00); // Track index
      assert(fread8u(input_image) == 0x00); // 0x00
      uint32_t pretrack_LBA = fread32u(input_image);

      uint8_t track_mode = fread8u(input_image);
      assert(fread8u(input_image) == track_number);    // Track number
      assert(fread8u(input_image) == 0x01); // Track index
      assert(fread8u(input_image) == 0x00); // 0x00
      uint32_t track_LBA = fread32u(input_image);

      printf ("\t\t\t\tTrack %d: Index 0 uses mode %s and starts at LBA 0x%X, ", i, (pretrack_mode == 0x41 ? "Mode2" : (pretrack_mode == 0x01 ? "Audio" : "Unknown")), pretrack_LBA);
      printf("Index 1 uses mode %s and starts at LBA 0x%X.\n", (track_mode == 0x41 ? "Mode2" : (track_mode == 0x01 ? "Audio" : "Unknown")), track_LBA);
    }

    // Skip junk
    // TODO: track the version number and re-enable this assertion, but only for NRG_VER_55
    //       assert(fread8u(input_image) == session_mode); // Mode
    fread8u(input_image);
    assert(fread8u(input_image) == 0xaa);         // 0xAA
    assert(fread8u(input_image) == 0x01);         // 0x01
    assert(fread8u(input_image) == 0x00);         // 0x00

    uint32_t session_end_LBA = fread32u(input_image);
    printf("\t\t\tSession ends at LBA 0x%X\n", session_end_LBA);

    session_number++;
  }
  else if (chunk_id == DAOI) {
    /**
     * DAOI (DAO Information) format:
     *   4  B   Chunk size (bytes) again        (= (# tracks * 30) + 22)
     *   14 B   UPC?
     *   1  B   Toc type                        (0x20 = Mode2, 0x00 = Audio?)
     *   1  B   close_cd?                       (1 = doesn't work, usually 0)
     *   1  B   First track                     (Usually 0x01)
     *   1  B   Last track                      (Usually = # tracks)
     * ---
     *   10 B   ISRC Code?
     *   4  B   Sector size
     *   4  B   Mode                            (mode2 = 0x03000001, audio = 0x07000001)
     *   4  B   Index0 start offset
     *   4  B   Index1 start offset             (= index0 + pregap length)
     *   4  B   Next offset                     (= index1 + track length
     * ... Repeat for each track in this session
     */
    // # tracks
    int number_tracks = (fread32u(input_image) - 22) / 30;

    // Skip UPC
    fseek(input_image, 14, SEEK_CUR);

    uint8_t toc_type    = fread8u(input_image);
    fread8u(input_image); // close cd
    uint8_t first_track = fread8u(input_image);
    uint8_t last_track  = fread8u(input_image);

    printf("DAOI at 0x%X:\tSize - %dB, Toc Type - 0x%X, First Track - 0x%X, Last Track - 0x%X\n", start_offset, chunk_size, toc_type, first_track, last_track);
    printf("\t\t\tSession has %d tracks:\n", number_tracks);


    int i = 1;
    for (i = 1; i <= number_tracks; i++)
    {
      // Skip ISRC Code
      fseek(input_image, 10, SEEK_CUR);

      uint32_t sector_size = fread32u(input_image);
      uint32_t mode        = fread32u(input_image);
      uint32_t index0      = fread32u(input_image);
      uint32_t index1      = fread32u(input_image);
      uint32_t next_offset = fread32u(input_image);
      printf("\t\t\t\tTrack %d: Sector Size - %d B, Mode - %s, index0 start - 0x%X, index1 start - 0x%X, Next offset - 0x%X\n", i, sector_size, (mode == 0x03000001 ? "Mode2" : (mode == 0x07000001 ? "Audio" : "Other")), index0, index1, next_offset);
    }
  }
  else if (chunk_id == DAOX) {
    /**
     * DAOX (DAO Information) format:
     *   4  B    Chunk size (bytes) again    ( = (# tracks * 42) + 22 )
     *   14 B    UPC?
     *   1  B    Toc type                    (0x20 = Mode2, 0x00 = Audio?)
     *   1  B    Close CD?                   (1 = doesn't work)
     *   1  B    First track                 (Usually 0x01)
     *   1  B    Last track                  (Usually = tracks)
     * ---
     *   10 B    ISRC Code?
     *   4  B    Sector size
     *   4  B    Mode                        (mode2 = 0x03000001, audio = 0x07000001)
     *   8  B    Index0 start offset
     *   8  B    Index1                      (= index0 + pregap length)
     *   8  B    Next offset                 (= index1 + track length)
     *   ... Repeat for each track in session
     *
     *  It looks like either Index0 or Index1 is the file location where the actual image data lies. DC images' audio data is just straight 00's.
     */
    // # tracks
    int number_tracks = (fread32u(input_image) - 22) / 30;

    // Skip UPC
    fseek(input_image, 14, SEEK_CUR);

    uint8_t toc_type    = fread8u(input_image);
    fread8u(input_image); // close cd
    uint8_t first_track = fread8u(input_image);
    uint8_t last_track  = fread8u(input_image);

    printf("DAOX at 0x%X:\tSize - %dB, Toc Type - 0x%X, First Track - 0x%X, Last Track - 0x%X\n", start_offset, chunk_size, toc_type, first_track, last_track);
    printf("\t\t\tSession has %d tracks:\n", number_tracks);

    int i = 1;
    for (i = 1; i <= number_tracks; i++)
    {
      // Skip ISRC Code
      fseek(input_image, 10, SEEK_CUR);

      uint32_t sector_size = fread32u(input_image);
      uint32_t mode        = fread32u(input_image);
      uint64_t index0      = fread64u(input_image);
      uint64_t index1      = fread64u(input_image);
      uint64_t next_offset = fread64u(input_image);
      printf("\t\t\t\tTrack %d: Sector Size - %d B, Mode - %s, index0 start - 0x%X, index1 start - 0x%X, Next offset - 0x%X\n", i, sector_size, (mode == 0x03000001 ? "Mode2" : (mode == 0x07000001 ? "Audio" : "Other")), index0, index1, next_offset);
    }
  }
  else if (chunk_id == CDTX) {
    /**
     * CDTX (CD Text) format:
     *   18 B           CD-text pack
     */
    printf("CDTX at 0x%X:\tSize - %dB\n", start_offset, chunk_size);
    fseek(input_image, chunk_size, SEEK_CUR);
  }
  else if (chunk_id == ETNF) {
    /**
     * ETNF (Extended Track Information) format:
     * Multisession / TAO Only
     *   4 B    Start offset
     *   4 B    Length (bytes)
     *   4 B    Mode                    (011 (0x03) = mode2/2336, 110 (0x06) = mode2/2352, 111 (0x07) = audio/2352)
     *   4 B    Start lba
     *   4 B    00
     * ... Repeat for each track (in session)
     */
    uint32_t track_offset = fread32u(input_image);
    uint32_t track_length = fread32u(input_image);
    uint32_t track_mode   = fread32u(input_image);
    uint32_t start_lba    = fread32u(input_image);
    assert(fread32u(input_image) == 0x00);

    printf("ENTF at 0x%X:\tSize - %d B, Track Offset - 0x%X, Track Length - %d B, Mode - %s, Start LBA - 0x%X\n", start_offset, chunk_size, track_offset, track_length, (track_mode == 0x03 ? "Mode2/2336" : (track_mode == 0x06 ? "Mode2/2352" : (track_mode == 0x07 ? "Audio/2352" : "Unknown"))), start_lba);
  }
  else if (chunk_id == ETN2) {
    /**
     * ETN2 (Extended Track Information) format:
     *   8 B    Start offset
     *   8 B    Length (bytes)
     *   4 B    Mode                    (011 (0x03) = mode2/2336, 110 (0x06) = mode2/2352, 111 (0x07) = audio/2352)
     *   4 B    Start lba
     *   8 B    00
     * ... Repeat for each track (in session)
     */
    uint64_t track_offset = fread64u(input_image);
    uint64_t track_length = fread64u(input_image);
    uint32_t track_mode   = fread32u(input_image);
    uint32_t start_lba    = fread32u(input_image);
    assert(fread64u(input_image) == 0x00);

    printf("ENT2 at 0x%X:\tSize - %d B, Track Offset - 0x%X, Track Length - %d B, Mode - %s, Start LBA - 0x%X\n", start_offset, chunk_size, track_offset, track_length, (track_mode == 0x03 ? "Mode2/2336" : (track_mode == 0x06 ? "Mode2/2352" : (track_mode == 0x07 ? "Audio/2352" : "Unknown"))), start_lba);
  }
  else if (chunk_id == SINF) {
    /**
     * SINF (Session Information) Format:
     *   4 B    Number tracks in session
     */
    uint32_t number_tracks = fread32u(input_image);

    printf("SINF at 0x%X:\tSize - %dB, Number of Tracks: %d\n", start_offset, chunk_size, number_tracks);
  }
  else if (chunk_id == MTYP) {
    /**
     * MTYP (Media Type?) Format:
     *   4 B         unknown
     */
    uint32_t mystery_int  = fread32u(input_image);

    printf("MTYP at 0x%X:\tSize - %dB, ? - 0x%X\n", start_offset, chunk_size, mystery_int);
  }
  else if (chunk_id == END) {
    printf("END! at 0x%X\n", start_offset);
    exit(EXIT_SUCCESS);
  }
  else {
    fprintf(stderr, "Unrecognized Chunk ID: 0x%X. Aborting.\n", start_offset, chunk_id);
    exit(EXIT_FAILURE);
  }
}