from pathlib import Path
from pgn_parser import pgn, parser
from enum import Enum
import sys

class Color(Enum):
    EMPTY = 0
    WHITE = 1
    BLACK = 2


class FakeSimulation: 
    
    
    def __init__(self, game):
        #list of moves when PGN is parsed
        self.game = None
    
    def parsePGN(self, PGN_path_location: str):
        pgn_path = Path(PGN_path_location)
        with pgn_path.openI("r", encoding = "utf-8") as file:
            pgn_string = file.read()
        self.game - parser.parse(pgn_string, actions=pgn.Actions())
    #       m1 = game.move(1)
    #       assert m1.white.san == "e4"

    def getMove(self, moveNum: int, color: Color):
        move = self.game.move(moveNum)
        if(color == Color.WHITE):
            return move.white.san
        elif(color == Color.BLACK):
            return move.black.san
        else:
            sys.stderr.write("failed to get a valid move\n")
            return -1

    






    
        



