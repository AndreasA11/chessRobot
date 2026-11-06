import sys
from FakeSimulation import FakeSimulation

def main():
    simulation_state = FakeSimulation()
    if(get_mode == 1):
        PNG_mode()
    elif (get_mode == 2):
        human_mode()
    return 0

if __name__ == "__main__":
    sys.exit(main())



def get_mode():
    mode = -1
    print("select a mode. Enter 1 for PNG mode or Enter 2 for cli human interaction\n")
    mode = int(input())
    if(mode == 1):
        print("Enter file path to PGN game\n")
    elif(mode == -1):
        sys.stderr.write("failed to get a valid mode\n")
    return mode
    


def PNG_mode():
    return 0


def human_mode():  
    return 0