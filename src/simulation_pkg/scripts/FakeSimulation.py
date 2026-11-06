from pathlib import Path
from pgn_parser import pgn, parser

class FakeSimulation: 
    
    
    def __init__(self, game):
        #list of moves when PGN is parsed
        self.game = None
    
    def parsePGN(self, PGN_path_location):
        pgn_path = Path(PGN_path_location)
        with pgn_path.openI("r", encoding = "utf-8") as file:
            pgn_string = file.read()
        self.game - parser.parse(pgn_string, actions=pgn.Actions())
    #       m1 = game.move(1)
    #       assert m1.white.san == "e4"







    
        



