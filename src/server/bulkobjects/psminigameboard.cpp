/*
* psminigameboard.cpp
*
* Copyright (C) 2008 Atomic Blue (info@planeshift.it, http://www.atomicblue.org)
*
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* as published by the Free Software Foundation (version 2 of the License)
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*
*/

#include <psconfig.h>
#include <ctype.h>

//=============================================================================
// Crystal Space Includes
//=============================================================================
#include <csutil/xmltiny.h>

//=============================================================================
// Project Includes
//=============================================================================
#include "util/minigame.h"
#include "util/psxmlparser.h"

//=============================================================================
// Local Includes
//=============================================================================
#include "psminigameboard.h"

using namespace psMiniGame;


//#define DEBUG_MINIGAMES

psMiniGameBoardDef::psMiniGameBoardDef(const uint8_t noCols, const uint8_t noRows,
                                       const char *layoutStr, const char *piecesStr,
                                       const uint8_t defPlayers, const int16_t options)
{
    // rows & columns setup
    rows = noRows;
    cols = noCols;

    // layout setup
    layoutSize = cols * rows / 2;
    if ((cols * rows % 2) != 0)
        layoutSize++;

    // Convert the layout string into a packed binary array
    layout = new uint8_t[layoutSize];
    PackLayoutString(layoutStr, layout);

    // Get the list of available pieces
    pieces = NULL;
    uint8_t numPieces = 0;
    
    // Pack the list of pieces
    numPieces = (uint8_t)strlen(piecesStr);
    size_t piecesSize = numPieces / 2;
    if (numPieces % 2)
        piecesSize++;

    pieces = new uint8_t[piecesSize];

    for (size_t i = 0; i < numPieces; i++)
    {
        uint8_t v = 0;
        if (isxdigit(piecesStr[i]))
        {
            char ch = toupper(piecesStr[i]);
            v = (uint8_t)(ch - '0');
            if (ch >= 'A')
                v -= (uint8_t)('A' - '0') - 10;
        }
        if (i % 2)
            pieces[i/2] |= v;
        else
            pieces[i/2] = (v << 4);
    }

    numPlayers = defPlayers;
    gameboardOptions = options;

    // default game rules
    playerTurnRule = RELAXED;
    movePieceTypeRule = PLACE_OR_MOVE;
    moveablePiecesRule = ANY_PIECE;
    movePiecesToRule = ANYWHERE;

    endgames.Empty();
}

psMiniGameBoardDef::~psMiniGameBoardDef()
{
    if (layout)
        delete[] layout;
    if (pieces)
        delete[] pieces;
}

void psMiniGameBoardDef::PackLayoutString(const char *layoutStr, uint8_t *packedLayout)
{
    for (size_t i = 0; i < strlen(layoutStr); i++)
    {
        uint8_t v = 0x0F;
        if (isxdigit(layoutStr[i]))
        {
            char ch = toupper(layoutStr[i]);
            v = (uint8_t)(ch - '0');
            if (ch >= 'A')
                v -= (uint8_t)('A' - '0') - 10;
        }
        if (i % 2)
            packedLayout[i/2] |= v;
        else
            packedLayout[i/2] = (v << 4);
    }
}

bool psMiniGameBoardDef::DetermineGameRules(csString rulesXMLstr, csString name)
{
    if (rulesXMLstr.StartsWith("<GameRules>", false))
    {
        csRef<iDocument> doc = ParseString(rulesXMLstr);
        if (doc)
        {
            csRef<iDocumentNode> root = doc->GetRoot();
            if (root)
            {
                csRef<iDocumentNode> topNode = root->GetNode("GameRules");
                if (topNode)
                {
                    csRef<iDocumentNode> rulesNode = topNode->GetNode("Rules");
                    if (rulesNode )
                    {
                        // PlayerTurns can be 'Strict' (order of players' moves enforced)
                        // or 'Relaxed' (default - free for all).
                        csString playerTurnsVal (rulesNode->GetAttributeValue("PlayerTurns"));
                        if (playerTurnsVal.Downcase() == "ordered")
                        {
                            playerTurnRule = ORDERED;
                        }   
                        else if (!playerTurnsVal.IsEmpty() && playerTurnsVal.Downcase() != "relaxed")
                        {
                            Error3("\"%s\" Rule PlayerTurns \"%s\" not recognised. Defaulting to \'Relaxed\'.",
                                   name.GetDataSafe(), playerTurnsVal.GetDataSafe());
                        }

                        // MoveType can be 'MoveOnly' (player can only move existing pieces),
                        // 'PlaceOnly' (player can only place new pieces on the board; cant move others),
                        // or 'PlaceOrMovePiece' (default - either move existing or place new pieces).
                        csString moveTypeVal (rulesNode->GetAttributeValue("MoveType"));
                        if (moveTypeVal.Downcase() == "moveonly")
                        {
                            movePieceTypeRule = MOVE_ONLY;
                        }
                        else if (moveTypeVal.Downcase() == "placeonly")
                        {
                            movePieceTypeRule = PLACE_ONLY;
                        }
                        else if (!moveTypeVal.IsEmpty() && moveTypeVal.Downcase() != "placeormovepiece")
                        {
                            Error3("\"%s\" Rule MoveType \"%s\" not recognised. Defaulting to \'PlaceOrMovePiece\'.",
                                   name.GetDataSafe(), moveTypeVal.GetDataSafe());
                        }

                        // MoveablePieces can be 'Own' (player can only move their own pieces) or
                        // 'Any' (default - player can move any piece in play).
                        csString moveablePiecesVal (rulesNode->GetAttributeValue("MoveablePieces"));
                        if (moveablePiecesVal.Downcase() == "own")
                        {
                            moveablePiecesRule = OWN_PIECES_ONLY;
                        }
                        else if (!moveablePiecesVal.IsEmpty() && moveablePiecesVal.Downcase() != "any")
                        {
                            Error3("\"%s\" Rule MoveablePieces \"%s\" not recognised. Defaulting to \'Any\'.",
                                   name.GetDataSafe(), moveablePiecesVal.GetDataSafe());
                        }

                        // MoveTo can be 'Vacancy' (player can move pieces to vacant squares only) or
                        // 'Anywhere' (default - can move to any square, vacant or occupied).
                        csString moveToVal (rulesNode->GetAttributeValue("MoveTo"));
                        if (moveToVal.Downcase() == "vacancy")
                        {
                            movePiecesToRule = VACANCY_ONLY;
                        }
                        else if (!moveToVal.IsEmpty() && moveToVal.Downcase() != "anywhere")
                        {
                            Error3("\"%s\" Rule MoveTo \"%s\" not recognised. Defaulting to \'Anywhere\'.",
                                   name.GetDataSafe(), moveToVal.GetDataSafe());
                        }
                        return true;
                    }
                }
            }
        }
    }
    else if (rulesXMLstr.IsEmpty())   // if no rules defined at all, dont worry - keep defaults
    {
        return true;
    }

    Error2("XML error in GameRules definition for \"%s\" .", name.GetDataSafe());
    return false;
}

bool psMiniGameBoardDef::DetermineEndgameSpecs(csString endgameXMLstr, csString name)
{
    if (endgameXMLstr.StartsWith("<MGEndGame>", false))
    {
        csRef<iDocument> doc = ParseString(endgameXMLstr);
        if (doc)
        {
            csRef<iDocumentNode> root = doc->GetRoot();
            if (root)
            {
                csRef<iDocumentNode> topNode = root->GetNode("MGEndGame");
                if (topNode)
                {
                    csRef<iDocumentNodeIterator> egNodes = topNode->GetNodes("EndGame");
                    if (egNodes)
                    {
                        while (egNodes->HasNext())
                        {
                            csRef<iDocumentNode> egNode = egNodes->Next();
                            if (egNode)
                            {
                                Endgame_Spec* endgame = new Endgame_Spec;
                                csString posAbsVal (egNode->GetAttributeValue("Coords"));
                                if (posAbsVal.Downcase() == "relative")
                                {
                                    endgame->positionsAbsolute = false;
                                }
                                else if (posAbsVal.Downcase() == "absolute")
                                {
                                    endgame->positionsAbsolute = true;
                                }
                                else
                                {
                                    delete endgame;
                                    Error2("Error in EndGame XML for \"%s\" minigame: Absolute/Relative setting misunderstood.",
                                           name.GetDataSafe());
                                    return false;
                                }
                                csString srcTileVal (egNode->GetAttributeValue("SourceTile"));
                                Endgame_TileType srcTile;
                                if (EvaluateTileTypeStr(srcTileVal, srcTile) && srcTile != FOLLOW_SOURCE_TILE)
                                {
                                    endgame->sourceTile = srcTile;
                                }
                                else if (endgame->positionsAbsolute == false)
                                {
                                    delete endgame;
                                    Error2("Error in EndGame XML for \"%s\" minigame: SourceTile setting misunderstood.",
                                           name.GetDataSafe());
                                    return false;
                                }

                                csRef<iDocumentNodeIterator> coordNodes = egNode->GetNodes("Coord");
                                if (coordNodes)
                                {
                                    while (coordNodes->HasNext())
                                    {
                                        csRef<iDocumentNode> coordNode = coordNodes->Next();
                                        if (coordNode)
                                        {
                                            Endgame_TileSpec* egTileSpec = new Endgame_TileSpec;
                                            int egCol = coordNode->GetAttributeValueAsInt("Col");
                                            int egRow = coordNode->GetAttributeValueAsInt("Row");
                                            if (abs(egCol) >= GAMEBOARD_MAX_COLS || abs(egRow) >= GAMEBOARD_MAX_ROWS)
                                            {
                                                delete endgame;
                                                delete egTileSpec;
                                                Error2("Error in EndGame XML for \"%s\" minigame: Col/Row spec out of range.",
                                                        name.GetDataSafe());
                                                return false;
                                            }

                                            csString tileVal (coordNode->GetAttributeValue("Tile"));
                                            Endgame_TileType tile;
                                            if (EvaluateTileTypeStr(tileVal, tile))
                                            {
                                                  egTileSpec->col = egCol;
                                                  egTileSpec->row = egRow;
                                                  egTileSpec->tile = tile;
                                            }
                                            else
                                            {
                                                delete endgame;
                                                delete egTileSpec;
                                                Error2("Error in EndGame XML for \"%s\" minigame: Tile setting misunderstood.",
                                                       name.GetDataSafe());
                                                return false;
                                            }

                                            endgame->endgameTiles.Push(egTileSpec); 
                                        }
                                    }
                                }
                                endgames.Push(endgame);
                            }
                        }
                    }

                   return true;
                }
            }
        }
    }
    else if (endgameXMLstr.IsEmpty())   // if no endgames defined at all, then no worries
    {
        return true;
    }

    Error2("XML error in Endgames definition for \"%s\" .", name.GetDataSafe());
    return false;
}

bool psMiniGameBoardDef::EvaluateTileTypeStr(csString TileTypeStr, Endgame_TileType& tileType)
{
    if (TileTypeStr.Length() == 1)
    {
        switch (TileTypeStr[0])
        {
            case 'A':
            case 'a': tileType = PLAYED_PIECE;        // tile has any played piece on
                break;
            case 'W':
            case 'w': tileType = WHITE_PIECE;         // tile has white piece
                break;
            case 'B':
            case 'b': tileType = BLACK_PIECE;         // tile has black piece
                break;
            case 'E':
            case 'e': tileType = EMPTY_TILE;          // empty tile
                break;
            case 'F':
            case 'f': tileType = FOLLOW_SOURCE_TILE;  // tile has piece as per first tile in pattern
                break;
            default: return false;
                break;
        }

        return true;
    }

    return false;
}

//---------------------------------------------------------------------------

psMiniGameBoard::psMiniGameBoard()
    : layout(0)
{
}

psMiniGameBoard::~psMiniGameBoard()
{
    if (layout)
        delete[] layout;
}

void psMiniGameBoard::Setup(psMiniGameBoardDef *newGameDef, uint8_t *preparedLayout)
{
    // Delete the previous layout
    if (layout)
        delete[] layout;

    gameBoardDef = newGameDef;

    layout = new uint8_t[gameBoardDef->layoutSize];

    if (!preparedLayout)
        memcpy(layout, gameBoardDef->layout, gameBoardDef->layoutSize);
    else
        memcpy(layout, preparedLayout, gameBoardDef->layoutSize);
}

uint8_t psMiniGameBoard::Get(uint8_t col, uint8_t row) const
{
    if (col >= gameBoardDef->cols || row >= gameBoardDef->rows || !layout)
        return DisabledTile;

    int idx = row*gameBoardDef->cols + col;
    uint8_t v = layout[idx/2];
    if (idx % 2)
        return v & 0x0F;
    else
        return (v & 0xF0) >> 4;
}

void psMiniGameBoard::Set(uint8_t col, uint8_t row, uint8_t state)
{
    if (col >= gameBoardDef->cols || row >= gameBoardDef->rows || !layout)
        return;

    int idx = row*gameBoardDef->cols + col;
    uint8_t v = layout[idx/2];
    if (idx % 2)
    {
        if ((v & 0x0F) != DisabledTile)
            layout[idx/2] = (v & 0xF0) + (state & 0x0F);
    }
    else
    {
        if ((v & 0xF0) >> 4 != DisabledTile)
            layout[idx/2] = (v & 0x0F) + ((state & 0x0F) << 4);
    }
}

bool psMiniGameBoard::DetermineEndgame(void)
{
    if (gameBoardDef->endgames.IsEmpty())
        return false;

    // look through each endgame spec individually...
    csArray<Endgame_Spec*>::Iterator egIterator(gameBoardDef->endgames.GetIterator());
    while (egIterator.HasNext())
    {
        size_t patternsMatched;
        uint8_t tileAtPos;
        Endgame_Spec* endgame = egIterator.Next();
        if (endgame->positionsAbsolute)
        {
            // endgame absolute pattern: check each position for the required pattern
            // ... and then through each tile for the endgame pattern
            patternsMatched = 0;
            csArray<Endgame_TileSpec*>::Iterator egTileIterator(endgame->endgameTiles.GetIterator());
            while (egTileIterator.HasNext())
            {
                Endgame_TileSpec* endgameTile = egTileIterator.Next();
                if (endgameTile->col > gameBoardDef->cols || endgameTile->row > gameBoardDef->rows ||
                    endgameTile->col < 0 || endgameTile->row < 0)
                    break;

                tileAtPos = Get(endgameTile->col, endgameTile->row);

                if (tileAtPos == DisabledTile || endgame->sourceTile == FOLLOW_SOURCE_TILE)
                    break;
                if (endgameTile->tile == PLAYED_PIECE && tileAtPos == EmptyTile)
                    break;
                if (endgameTile->tile == EMPTY_TILE && tileAtPos != EmptyTile)
                    break;
                if (endgameTile->tile == WHITE_PIECE && (tileAtPos < White1 || tileAtPos > White7))
                    break;
                if (endgameTile->tile == BLACK_PIECE && (tileAtPos < Black1 || tileAtPos > Black7))
                    break;
                if (endgameTile->tile == FOLLOW_SOURCE_TILE)
                    break;

                // if here, then the pattern has another match
                patternsMatched++;
            }

            // if all patterns matched, a winner
            if (endgame->endgameTiles.GetSize() == patternsMatched)
            {
                return true;
            }
        }
        else
        {
            // endgame relative pattern: check each piece in play for pattern relative to the piece
            uint8_t colCount, rowCount;
            for (rowCount=0; rowCount<gameBoardDef->rows; rowCount++)
            {
                for (colCount=0; colCount<gameBoardDef->cols; colCount++)
                {
                    // look for next initial played piece
                    uint8_t initialTile = Get(colCount, rowCount);
                    if ((initialTile >= White1 && initialTile <= White7 &&
                         (endgame->sourceTile == WHITE_PIECE || endgame->sourceTile == PLAYED_PIECE)) ||
                        (initialTile >= Black1 && initialTile <= Black7 &&
                         (endgame->sourceTile == BLACK_PIECE || endgame->sourceTile == PLAYED_PIECE)))
                    {
                        // ... and then through each tile for the endgame pattern
                        patternsMatched = 0;
                        csArray<Endgame_TileSpec*>::Iterator egTileIterator(endgame->endgameTiles.GetIterator());
                        while (egTileIterator.HasNext())
                        {
                            Endgame_TileSpec* endgameTile = egTileIterator.Next();
                            if (colCount+endgameTile->col > gameBoardDef->cols || rowCount+endgameTile->row > gameBoardDef->rows ||
                                colCount+endgameTile->col < 0 || rowCount+endgameTile->row < 0)
                                break;
                            tileAtPos = Get(colCount+endgameTile->col, rowCount+endgameTile->row);
                            if (tileAtPos == DisabledTile || endgame->sourceTile == FOLLOW_SOURCE_TILE)
                                break;
                            if (endgameTile->tile == PLAYED_PIECE && (tileAtPos < White1 || tileAtPos > Black7))
                                break;
                            if (endgameTile->tile == WHITE_PIECE && (tileAtPos < White1 || tileAtPos > White7))
                                break;
                            if (endgameTile->tile == BLACK_PIECE && (tileAtPos < Black1 || tileAtPos > Black7))
                                break;
                            if (endgameTile->tile == EMPTY_TILE && tileAtPos != EmptyTile)
                                break;
                            if (endgameTile->tile == FOLLOW_SOURCE_TILE && tileAtPos != initialTile)
                                break;

                            // if here, then the pattern has another match
                            patternsMatched++;
                        }

                        // if all patterns matched, a winner
                        if (endgame->endgameTiles.GetSize() == patternsMatched)
                        {
                            return true;
                        }
                    }
                }
            }
        }
    }

    return false;
}

