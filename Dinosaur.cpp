// compile with: clang++ -std=c++20 -Wall -Werror -Wextra -Wpedantic -g3 -o FinalProject FinalProject.cpp
// run with: ./fishies 2> /dev/null
// run with: ./fishies 2> debugoutput.txt
//  "2>" redirect standard error (STDERR; cerr)
//  /dev/null is a "virtual file" which discard contents

// Works best in Visual Studio Code if you set:
//   Settings -> Features -> Terminal -> Local Echo Latency Threshold = -1

// https://en.wikipedia.org/wiki/ANSI_escape_code#3-bit_and_4-bit

#include <iostream>
#include <vector>
#include <string>
#include <random>
#include <chrono>    // for dealing with time intervals
#include <cmath>     // for max() and min()
#include <termios.h> // to control terminal modes
#include <unistd.h>  // for read()
#include <fcntl.h>   // to enable / disable non-blocking read()
#include <stdlib.h>
#include <experimental/random>

// Because we are only using #includes from the standard, names shouldn't conflict
using namespace std;

// Constants

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-const-variable"

const char NULL_CHAR{'z'};
const char UP_CHAR{'w'};
const char DOWN_CHAR{'s'};
const char LEFT_CHAR{'a'};
const char RIGHT_CHAR{'d'};
const char QUIT_CHAR{'q'};
const char FREEZE_CHAR{'f'};
const char CREATE_CHAR{'c'};
const char BLOCKING_CHAR{'b'};
const char COMMAND_CHAR{'o'};
const char JUMP_CHAR{' '};
const char EMPTY_CHAR{};

const string ANSI_START{"\033["};
const string START_COLOUR_PREFIX{"1;"};
const string START_COLOUR_SUFFIX{"m"};
const string STOP_COLOUR{"\033[0m"};

const unsigned int COLOUR_IGNORE{0}; // this is a little dangerous but should work out OK
const unsigned int COLOUR_BLACK{30};
const unsigned int COLOUR_RED{31};
const unsigned int COLOUR_GREEN{32};
const unsigned int COLOUR_YELLOW{33};
const unsigned int COLOUR_BLUE{34};
const unsigned int COLOUR_MAGENTA{35};
const unsigned int COLOUR_CYAN{36};
const unsigned int COLOUR_WHITE{37};

const unsigned short MOVING_NOWHERE{0};
const unsigned short MOVING_LEFT{1};
const unsigned short MOVING_RIGHT{2};
const unsigned short MOVING_UP{3};
const unsigned short MOVING_DOWN{4};

struct termios initialTerm;
default_random_engine generator;
uniform_int_distribution<unsigned int> cloudvelocity(1, 5);
uniform_int_distribution<unsigned int> obvelocity(2, 6);

int screenWidth;
int screenLength;
int t {0};
unsigned int score{0};
#pragma clang diagnostic pop

uniform_int_distribution<int> obspawns(100, screenLength); //We couldnt end up using any of these distributions as they broke, see more info under struct cloud documentation
uniform_int_distribution<int> cloudrows(0, (screenWidth / 2) + 15);
uniform_int_distribution<int> cloudcols(0, screenLength);

//uniform_int_distribution<int> cloudcols(0, screenLength);
//uniform_int_distribution<int> cloudrows(0, screenWidth / 2 + 15);

// Types

struct position
{
    int row{0};
    int col{0};
};

struct ground
{
    position position{};
    float velocity {1.0};
};

struct player
{
    position position{};
};

struct cloud
{
    position position{experimental::randint(0, screenWidth / 2 + screenWidth / 10), experimental::randint(0, screenLength)}; //This code makes sure the clouds spawn outside of the play area
    //position position{cloudrows(generator), cloudcols(generator)}; //!!! When this "proper" code is used, the game compiles but instantly seg faults when ran. I believe this code doesn't like when screenWidth and screenLength are used, as it also breaks the game when used for the initial positions of the obstacles aswell
    unsigned int velocity{cloudvelocity(generator)}; //Determines how fast the clouds move. They will move anywhere from 1 to 5 units per tick depending on a uniform distribution
    unsigned int destructSequence{0}; //This variable is used to destroy visible parts of the cloud in iterations once it reaches the end of the screen, until the cloud is completely invisible
};

struct obstacle
{
    position position{1, 1};
    unsigned int velocity{obvelocity(generator)};
};

typedef vector<cloud> cloudvector;
typedef vector<obstacle> obvector;

//------------------------------------------------------------------------------------------------------------------------!MAGIC!-------------------------------------------------------------------------------------------------------------------------
// These two functions are taken from StackExchange and are
// all of the "magic" in this code.
auto SetupScreenAndInput() -> void
{
    struct termios newTerm;
    // Load the current terminal attributes for STDIN and store them in a global
    tcgetattr(fileno(stdin), &initialTerm);
    newTerm = initialTerm;
    // Mask out terminal echo and enable "noncanonical mode"
    // " ... input is available immediately (without the user having to type
    // a line-delimiter character), no input processing is performed ..."
    newTerm.c_lflag &= ~ICANON;
    newTerm.c_lflag &= ~ECHO;
    newTerm.c_cc[VMIN] = 1;

    // Set the terminal attributes for STDIN immediately
    auto result{tcsetattr(fileno(stdin), TCSANOW, &newTerm)};
    if (result < 0)
    {
        cerr << "Error setting terminal attributes [" << result << "]" << endl;
    }
}
auto TeardownScreenAndInput() -> void
{
    // Reset STDIO to its original settings
    tcsetattr(fileno(stdin), TCSANOW, &initialTerm);
}
auto SetNonblockingReadState(bool desiredState = true) -> void
{
    auto currentFlags{fcntl(0, F_GETFL)};
    if (desiredState)
    {
        fcntl(0, F_SETFL, (currentFlags | O_NONBLOCK));
    }
    else
    {
        fcntl(0, F_SETFL, (currentFlags & (~O_NONBLOCK)));
    }
    cerr << "SetNonblockingReadState [" << desiredState << "]" << endl;
}
// Everything from here on is based on ANSI codes
// Note the use of "flush" after every write to ensure the screen updates
auto ClearScreen() -> void { cout << ANSI_START << "2J" << flush; }
auto MoveTo(unsigned int x, unsigned int y) -> void { cout << ANSI_START << x << ";" << y << "H" << flush; }
auto HideCursor() -> void { cout << ANSI_START << "?25l" << flush; }
auto ShowCursor() -> void { cout << ANSI_START << "?25h" << flush; }
auto GetTerminalSize() -> position
{
    // This feels sketchy but is actually about the only way to make this work
    MoveTo(999, 999);
    cout << ANSI_START << "6n" << flush;
    string responseString;
    char currentChar{static_cast<char>(getchar())};
    while (currentChar != 'R')
    {
        responseString += currentChar;
        currentChar = getchar();
    }
    // format is ESC[nnn;mmm ... so remove the first 2 characters + split on ; + convert to unsigned int
    // cerr << responseString << endl;
    responseString.erase(0, 2);
    // cerr << responseString << endl;
    auto semicolonLocation = responseString.find(";");
    // cerr << "[" << semicolonLocation << "]" << endl;
    auto rowsString{responseString.substr(0, semicolonLocation)};
    auto colsString{responseString.substr((semicolonLocation + 1), responseString.size())};
    // cerr << "[" << rowsString << "][" << colsString << "]" << endl;
    auto rows = stoul(rowsString);
    auto cols = stoul(colsString);
    position returnSize{static_cast<int>(rows), static_cast<int>(cols)};
    // cerr << "[" << returnSize.row << "," << returnSize.col << "]" << endl;
    return returnSize;
}
//------------------------------------------------------------------------------------------------------------------------!MAGIC!-------------------------------------------------------------------------------------------------------------------------

//This function deals with animating the individual cloud entrances and exits, aswell as its normal movement across the screen. An index based for loop was used as the location of the clouds were necessary so we could mark clouds at certain positions that were going to be destroyed 
auto drawClouds(cloudvector &clouds) -> void
{
    vector<unsigned int> markForDeath;
    for (unsigned int cloud = 0; cloud < clouds.size(); cloud += 1)
    {
        // These are clouds being generated at the right side of the screen.
        if (clouds.at(cloud).position.col == screenLength - 1)
        {
            MoveTo(clouds.at(cloud).position.row, clouds.at(cloud).position.col);
            cout << "_" << flush;
            MoveTo(clouds.at(cloud).position.row + 1, clouds.at(cloud).position.col);
            cout << "(" << flush;
            MoveTo(clouds.at(cloud).position.row + 2, clouds.at(cloud).position.col);
            cout << " " << flush;
        }
        else if (clouds.at(cloud).position.col == screenLength - 2)
        {
            MoveTo(clouds.at(cloud).position.row, clouds.at(cloud).position.col);
            cout << "_(" << flush;
            MoveTo(clouds.at(cloud).position.row + 1, clouds.at(cloud).position.col);
            cout << "(_" << flush;
            MoveTo(clouds.at(cloud).position.row + 2, clouds.at(cloud).position.col);
            cout << " (" << flush;
        }
        else if (clouds.at(cloud).position.col == screenLength - 3)
        {
            MoveTo(clouds.at(cloud).position.row, clouds.at(cloud).position.col);
            cout << "_( " << flush;
            MoveTo(clouds.at(cloud).position.row + 1, clouds.at(cloud).position.col);
            cout << "(_ " << flush;
            MoveTo(clouds.at(cloud).position.row + 2, clouds.at(cloud).position.col);
            cout << " (_" << flush;
        }
        else if (clouds.at(cloud).position.col == screenLength - 4)
        {
            MoveTo(clouds.at(cloud).position.row, clouds.at(cloud).position.col);
            cout << "_(  " << flush;
            MoveTo(clouds.at(cloud).position.row + 1, clouds.at(cloud).position.col);
            cout << "(_  " << flush;
            MoveTo(clouds.at(cloud).position.row + 2, clouds.at(cloud).position.col);
            cout << " (_)" << flush;
        }
        else if (clouds.at(cloud).position.col == screenLength - 5)
        {
            MoveTo(clouds.at(cloud).position.row, clouds.at(cloud).position.col);
            cout << "_(  )" << flush;
            MoveTo(clouds.at(cloud).position.row + 1, clouds.at(cloud).position.col);
            cout << "(_   " << flush;
            MoveTo(clouds.at(cloud).position.row + 2, clouds.at(cloud).position.col);
            cout << " (_) " << flush;
        }
        else if (clouds.at(cloud).position.col == screenLength - 6)
        {
            MoveTo(clouds.at(cloud).position.row, clouds.at(cloud).position.col);
            cout << "_(  )_" << flush;
            MoveTo(clouds.at(cloud).position.row + 1, clouds.at(cloud).position.col);
            cout << "(_   _" << flush;
            MoveTo(clouds.at(cloud).position.row + 2, clouds.at(cloud).position.col);
            cout << " (_) (" << flush;
        }
        else if (clouds.at(cloud).position.col == screenLength - 7)
        {
            MoveTo(clouds.at(cloud).position.row, clouds.at(cloud).position.col);
            cout << "_(  )_(" << flush;
            MoveTo(clouds.at(cloud).position.row + 1, clouds.at(cloud).position.col);
            cout << "(_   _ " << flush;
            MoveTo(clouds.at(cloud).position.row + 2, clouds.at(cloud).position.col);
            cout << " (_) (_" << flush;
        }
        else if (clouds.at(cloud).position.col == screenLength - 8)
        {
            MoveTo(clouds.at(cloud).position.row, clouds.at(cloud).position.col);
            cout << "_(  )_( " << flush;
            MoveTo(clouds.at(cloud).position.row + 1, clouds.at(cloud).position.col);
            cout << "(_   _  " << flush;
            MoveTo(clouds.at(cloud).position.row + 2, clouds.at(cloud).position.col);
            cout << " (_) (__" << flush;
        }
        else if (clouds.at(cloud).position.col == screenLength - 9)
        {
            MoveTo(clouds.at(cloud).position.row, clouds.at(cloud).position.col);
            cout << "_(  )_( )" << flush;
            MoveTo(clouds.at(cloud).position.row + 1, clouds.at(cloud).position.col);
            cout << "(_   _   " << flush;
            MoveTo(clouds.at(cloud).position.row + 2, clouds.at(cloud).position.col);
            cout << " (_) (__" << flush;
        }
        else if (clouds.at(cloud).position.col == screenLength - 10)
        {
            MoveTo(clouds.at(cloud).position.row, clouds.at(cloud).position.col);
            cout << "_(  )_( )_" << flush;
            MoveTo(clouds.at(cloud).position.row + 1, clouds.at(cloud).position.col);
            cout << "(_   _    " << flush;
            MoveTo(clouds.at(cloud).position.row + 2, clouds.at(cloud).position.col);
            cout << " (_) (__)" << flush;
        }
        else if (clouds.at(cloud).position.col == screenLength - 11)
        {
            MoveTo(clouds.at(cloud).position.row, clouds.at(cloud).position.col);
            cout << "_(  )_( )_" << flush;
            MoveTo(clouds.at(cloud).position.row + 1, clouds.at(cloud).position.col);
            cout << "(_   _    _" << flush;
            MoveTo(clouds.at(cloud).position.row + 2, clouds.at(cloud).position.col);
            cout << " (_) (__)" << flush;
        }
        // ... And these are clouds being destroyed at the left side of the screen. 
        
        else if (clouds.at(cloud).position.col <= 0 and clouds.at(cloud).destructSequence == 0) //once this part of the statement is triggered, the destructsequence cascade begins and the cloud will die over the next 11 ticks
        {
            clouds.at(cloud).destructSequence += clouds.at(cloud).velocity;
            MoveTo(clouds.at(cloud).position.row, 0);
            cout << "(  )_( )_" << flush;
            MoveTo(clouds.at(cloud).position.row + 1, 0);
            cout << "_   _    _)" << flush;
            MoveTo(clouds.at(cloud).position.row + 2, 0);
            cout << "(_) (__)" << flush;
        }

        else if (clouds.at(cloud).destructSequence == 1)
        {
            clouds.at(cloud).destructSequence += clouds.at(cloud).velocity;
            MoveTo(clouds.at(cloud).position.row, 0);
            cout << "  )_( )_" << flush;
            MoveTo(clouds.at(cloud).position.row + 1, 0);
            cout << "   _    _)" << flush;
            MoveTo(clouds.at(cloud).position.row + 2, 0);
            cout << "_) (__)" << flush;
        }

        else if (clouds.at(cloud).destructSequence == 2)
        {
            clouds.at(cloud).destructSequence += clouds.at(cloud).velocity;
            MoveTo(clouds.at(cloud).position.row, 0);
            cout << " )_( )_" << flush;
            MoveTo(clouds.at(cloud).position.row + 1, 0);
            cout << "  _    _)" << flush;
            MoveTo(clouds.at(cloud).position.row + 2, 0);
            cout << ") (__)" << flush;
        }

        else if (clouds.at(cloud).destructSequence == 3)
        {
            clouds.at(cloud).destructSequence += clouds.at(cloud).velocity;
            MoveTo(clouds.at(cloud).position.row, 0);
            cout << ")_( )_" << flush;
            MoveTo(clouds.at(cloud).position.row + 1, 0);
            cout << " _    _)" << flush;
            MoveTo(clouds.at(cloud).position.row + 2, 0);
            cout << " (__)" << flush;
        }

        else if (clouds.at(cloud).destructSequence == 4)
        {
            clouds.at(cloud).destructSequence += clouds.at(cloud).velocity;
            MoveTo(clouds.at(cloud).position.row, 0);
            cout << "_( )_" << flush;
            MoveTo(clouds.at(cloud).position.row + 1, 0);
            cout << "_    _)" << flush;
            MoveTo(clouds.at(cloud).position.row + 2, 0);
            cout << "(__)" << flush;
        }

        else if (clouds.at(cloud).destructSequence == 5)
        {
            clouds.at(cloud).destructSequence += clouds.at(cloud).velocity;
            MoveTo(clouds.at(cloud).position.row, 0);
            cout << "( )_" << flush;
            MoveTo(clouds.at(cloud).position.row + 1, 0);
            cout << "    _)" << flush;
            MoveTo(clouds.at(cloud).position.row + 2, 0);
            cout << "__)" << flush;
        }

        else if (clouds.at(cloud).destructSequence == 6)
        {
            clouds.at(cloud).destructSequence += clouds.at(cloud).velocity;
            MoveTo(clouds.at(cloud).position.row, 0);
            cout << " )_" << flush;
            MoveTo(clouds.at(cloud).position.row + 1, 0);
            cout << "   _)" << flush;
            MoveTo(clouds.at(cloud).position.row + 2, 0);
            cout << "_)" << flush;
        }

        else if (clouds.at(cloud).destructSequence == 7)
        {
            clouds.at(cloud).destructSequence += clouds.at(cloud).velocity;
            MoveTo(clouds.at(cloud).position.row, 0);
            cout << ")_" << flush;
            MoveTo(clouds.at(cloud).position.row + 1, 0);
            cout << "  _)" << flush;
            MoveTo(clouds.at(cloud).position.row + 2, 0);
            cout << ")" << flush;
        }

        else if (clouds.at(cloud).destructSequence == 8)
        {
            clouds.at(cloud).destructSequence += clouds.at(cloud).velocity;
            MoveTo(clouds.at(cloud).position.row, 0);
            cout << "_" << flush;
            MoveTo(clouds.at(cloud).position.row + 1, 0);
            cout << " _)" << flush;
            MoveTo(clouds.at(cloud).position.row + 2, 0);
            cout << "" << flush;
        }

        else if (clouds.at(cloud).destructSequence == 9)
        {
            clouds.at(cloud).destructSequence += clouds.at(cloud).velocity;
            MoveTo(clouds.at(cloud).position.row, 0);
            cout << "" << flush;
            MoveTo(clouds.at(cloud).position.row + 1, 0);
            cout << "_)" << flush;
            MoveTo(clouds.at(cloud).position.row + 2, 0);
            cout << "" << flush;
        }

        else if (clouds.at(cloud).destructSequence == 10)
        {
            clouds.at(cloud).destructSequence += clouds.at(cloud).velocity;
            MoveTo(clouds.at(cloud).position.row, 0);
            cout << "" << flush;
            MoveTo(clouds.at(cloud).position.row + 1, 0);
            cout << ")" << flush;
            MoveTo(clouds.at(cloud).position.row + 2, 0);
            cout << "" << flush;
        }

        else
        {
            MoveTo(clouds.at(cloud).position.row, clouds.at(cloud).position.col);
            cout << "_(  )_( )_" << flush;
            MoveTo(clouds.at(cloud).position.row + 1, clouds.at(cloud).position.col);
            cout << "(_   _    _)" << flush;
            MoveTo(clouds.at(cloud).position.row + 2, clouds.at(cloud).position.col);
            cout << " (_) (__)" << flush;
        }
        if (clouds.at(cloud).destructSequence >= 11)
        {
            markForDeath.push_back(cloud); //cloud positions are marked but not destroyed yet, as destroying them here causes seg faults. 
        }
    }
    for (unsigned int element : markForDeath)
        clouds.erase(clouds.begin() + (element)); //according to the documentation, .erase fills in the erased position immediately. It only takes iterators so we had to use clouds.begin()
}

//This function updates the position of each cloud according to its inherent velocity. An index based approach was beggrudgingly used as the below function would break the clouds for an unknown reason
auto moveClouds(cloudvector &clouds) -> void
{
    for (unsigned int cloud = 0; cloud < clouds.size(); cloud += 1)
    {
        clouds.at(cloud).position.col -= clouds.at(cloud).velocity;
    }
}

//this code broke the clouds even though a range based for loop would be more suitable here
// auto moveClouds(cloudvector &clouds) -> void
// {
//     for (cloud currentCloud: clouds)
//     {
//         currentCloud.position.col -= currentCloud.velocity;
//     }
// }

//same as drawClouds but for the obstacles, there is no "destruct sequence" here however as the obstacles are reused
auto drawObstacles(obstacle &currentObstacle) -> void
{

    if (currentObstacle.position.col == -1)
    {
        MoveTo(currentObstacle.position.row, 0);
        cout << "\033[1m\033[32m"
             << " | " << flush;
        MoveTo(currentObstacle.position.row + 1, 0);
        cout << "_| " << flush;
        MoveTo(currentObstacle.position.row + 2, 0);
        cout << "|  "
             << "\033[0m" << flush;
    }

    else if (currentObstacle.position.col == -2)
    {
        MoveTo(currentObstacle.position.row, 0);
        cout << "\033[1m\033[32m"
             << "| " << flush;
        MoveTo(currentObstacle.position.row + 1, 0);
        cout << "| " << flush;
        MoveTo(currentObstacle.position.row + 2, 0);
        cout << "  "
             << "\033[0m" << flush;
    }

    else if (currentObstacle.position.col <= -3)
    {
        MoveTo(currentObstacle.position.row, 0);
        cout << "\033[1m\033[32m"
             << " " << flush;
        MoveTo(currentObstacle.position.row + 1, 0);
        cout << " " << flush;
        MoveTo(currentObstacle.position.row + 2, 0);
        cout << " "
             << "\033[0m" << flush;
        currentObstacle.position.col = screenLength;
        currentObstacle.velocity = obvelocity(generator);
    }

    else
    {
        MoveTo(currentObstacle.position.row, currentObstacle.position.col);
        cout << "\033[1m\033[32m"
             << "| | " << flush;
        MoveTo(currentObstacle.position.row + 1, currentObstacle.position.col);
        cout << "|_| " << flush;
        MoveTo(currentObstacle.position.row + 2, currentObstacle.position.col);
        cout << " |  "
             << "\033[0m" << flush;
    }
}
//same as moveClouds but for the obstacles
auto moveObstacles(obstacle &currentObstacle) -> void
{

    currentObstacle.position.col -= currentObstacle.velocity;
}
//changes the players current row and column to follow that of a parabola for realistic movement
auto jumpPlayer(player &player) -> void
{
    //Calculates the vertical component of the player as a quadratic, providing realistic movement. 
    //t represents the "independent variable" it can be thought of as "frames" of an animation
    t += 1;
    if (t <= 6) //once the jump animation begins, it can not be stopped until t = 0 again this both stops players from jumping through the sky and ensures the jump cant be cancelled early
    {
        player.position.row = (screenWidth - 1) - (-pow((t - 3), 2) + 9); //This was the function for the parabola
        player.position.col += 2;
    }
    else
    {
        t = 0;
    }
}
//Same as drawClouds, but obviously a lot shorter as it only has 1 possible visual state it can be in, and only 1 row
auto drawPlayer(player &player) -> void
{
    MoveTo(player.position.row, player.position.col);
    cout << "\033[1m\033[34m"
         << "Σ(⊃≧ᴗ≦)⊃" //cute 
         << "\033[0m" << flush;
}
//The ground is drawn at the start but isn't touched again as it doesn't move
auto drawGround(ground &ground) -> void
{
    MoveTo(ground.position.row, ground.position.col);
    cout << "\033[1m\033[30m";
    for (int i = 0; i < screenLength - 1; i++)
    {
        cout << "‾";
    }
    cout << "‾"
         << "\033[0m" << flush;
}

//This function positions the scoreboard at the top center and colors it red.
auto drawScore(position scoreposition, unsigned int ticks) -> void
{
    MoveTo(scoreposition.row, scoreposition.col);
    cout << "\033[1m\033[91m"
         << "Score: " << score << " Time: " << static_cast<float>(ticks / 10) << "s"
         << "\033[0m" << flush;
}

//Fixes the end position so that the command line does not appear after the scoreboard (ruining the visuals)
auto endPosition() -> void
{
    MoveTo(screenWidth, screenLength/2);
}

//Function will ensure no character column of the player is touching any character column of the obstacle below 3 units. If one or more characters are touching, this function signals that the game is over.
auto checkCollision(obstacle ob, player character) -> bool
{
    bool alreadyScored{false};
    bool gameOver {false};
    for (int characterlength = -1 ; characterlength <= 10; characterlength += 1) //we oversized the hitboxes of the player as the terminal was being a bit too generous
    {
        for (int oblength = 0; oblength <= 3; oblength += 1)
        {
            if ((character.position.col + characterlength) == (ob.position.col + oblength))
            {
                if (character.position.row >= (screenWidth - 3))
                {
                    gameOver = true;
                }
                else if (character.position.row < (screenWidth - 3) and oblength == 3 and characterlength >= 5 and alreadyScored == false) //If a character column of the player touches a character column of the obstacle but isnt below 3 units, 1 score is added
                {
                    score += 1;
                    alreadyScored = true;
                }
            }
        }
    }
    return gameOver;
}

//This function prints some creative ascii art when the game ends
auto gameOverScreen( unsigned int ticks) -> void{

        MoveTo(screenWidth/2 - 13, screenLength/2 - 19);
        cout << "┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼" << flush;

        MoveTo(screenWidth/2 - 12, screenLength/2 - 19);
        cout << "███▀▀▀██┼███▀▀▀███┼███▀█▄█▀███┼██▀▀▀" << flush;

        MoveTo(screenWidth/2 - 11, screenLength/2 - 19);
        cout << "██┼┼┼┼██┼██┼┼┼┼┼██┼██┼┼┼█┼┼┼██┼██┼┼┼" << flush;

        MoveTo(screenWidth/2 - 10, screenLength/2 - 19);
        cout << "██┼┼┼▄▄▄┼██▄▄▄▄▄██┼██┼┼┼▀┼┼┼██┼██▀▀▀" << flush;

        MoveTo(screenWidth/2 - 9, screenLength/2 - 19);
        cout << "██┼┼┼┼██┼██┼┼┼┼┼██┼██┼┼┼┼┼┼┼██┼██┼┼┼" << flush;

        MoveTo(screenWidth/2 - 8, screenLength/2 - 19);
        cout << "███▄▄▄██┼██┼┼┼┼┼██┼██┼┼┼┼┼┼┼██┼██▄▄▄" << flush;

        MoveTo(screenWidth/2 - 7, screenLength/2 - 19);
        cout << "┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼" << flush;

        MoveTo(screenWidth/2 - 6, screenLength/2 - 19);
        cout << "███▀▀▀███┼▀███┼┼██▀┼██▀▀▀┼██▀▀▀▀██▄┼" << flush;

        MoveTo(screenWidth/2 - 5, screenLength/2 - 19);
        cout << "██┼┼┼┼┼██┼┼┼██┼┼██┼┼██┼┼┼┼██┼┼┼┼┼██┼" << flush;

        MoveTo(screenWidth/2 - 4, screenLength/2 - 19);
        cout << "██┼┼┼┼┼██┼┼┼██┼┼██┼┼██▀▀▀┼██▄▄▄▄▄▀▀┼" << flush;

        MoveTo(screenWidth/2 - 3, screenLength/2 - 19);
        cout << "██┼┼┼┼┼██┼┼┼██┼┼█▀┼┼██┼┼┼┼██┼┼┼┼┼██┼" << flush;

        MoveTo(screenWidth/2 - 2, screenLength/2 - 19);
        cout << "███▄▄▄███┼┼┼─▀█▀┼┼─┼██▄▄▄┼██┼┼┼┼┼██▄" << flush;

        MoveTo(screenWidth/2 - 1, screenLength/2 - 19);
        cout << "┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼" << flush;

        MoveTo(screenWidth/2 , screenLength/2 - 19);
        cout << "┼┼┼┼┼┼┼┼██┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼██┼┼┼┼┼┼┼┼┼" << flush;

        MoveTo(screenWidth/2 + 1, screenLength/2 - 19);
        cout << "┼┼┼┼┼┼████▄┼┼┼▄▄▄▄▄▄▄┼┼┼▄████┼┼┼┼┼┼┼" << flush;

        MoveTo(screenWidth/2 + 2, screenLength/2 - 19);
        cout << "┼┼┼┼┼┼┼┼┼▀▀█▄█████████▄█▀▀┼┼┼┼┼┼┼┼┼┼" << flush;

        MoveTo(screenWidth/2 + 3, screenLength/2 - 19);
        cout << "┼┼┼┼┼┼┼┼┼┼┼█████████████┼┼┼┼┼┼┼┼┼┼┼┼" << flush;

        MoveTo(screenWidth/2 + 4, screenLength/2 - 19);
        cout << "┼┼┼┼┼┼┼┼┼┼┼██▀▀▀███▀▀▀██┼┼┼┼┼┼┼┼┼┼┼┼" << flush;

        MoveTo(screenWidth/2 + 5, screenLength/2 - 19);
        cout << "┼┼┼┼┼┼┼┼┼┼┼██┼┼┼███┼┼┼██┼┼┼┼┼┼┼┼┼┼┼┼" << flush;

        MoveTo(screenWidth/2 + 6, screenLength/2 - 19);
        cout << "┼┼┼┼┼┼┼┼┼┼┼█████▀▄▀█████┼┼┼┼┼┼┼┼┼┼┼┼" << flush;

        MoveTo(screenWidth/2 + 7, screenLength/2 - 19);
        cout << "┼┼┼┼┼┼┼┼┼┼┼┼███████████┼┼┼┼┼┼┼┼┼┼┼┼┼" << flush;

        MoveTo(screenWidth/2 + 8, screenLength/2 - 19);
        cout << "┼┼┼┼┼┼┼┼▄▄▄██┼┼█▀█▀█┼┼██▄▄▄┼┼┼┼┼┼┼┼┼" << flush;

        MoveTo(screenWidth/2 + 9, screenLength/2 - 19);
        cout << "┼┼┼┼┼┼┼┼▀▀██┼┼┼┼┼┼┼┼┼┼┼██▀▀┼┼┼┼┼┼┼┼┼" << flush;

        MoveTo(screenWidth/2 + 10, screenLength/2 - 19);
        cout << "┼┼┼┼┼┼┼┼┼┼▀▀┼┼┼┼┼┼┼┼┼┼┼▀▀┼┼┼┼┼┼┼┼┼┼┼" << flush;

        MoveTo(screenWidth/2 + 11, screenLength/2 - 19);
        cout << "┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼┼" << flush;

        cout << "\033[1m\033[91m"
         << "Score: " << score << " Time: " << static_cast<float>(ticks / 10) << "s"
         << "\033[0m" << flush;
        endPosition();
}

//if the right side of the player touches the side of the screen, the function signals that the process for a win should begin
auto checkWon( player character) -> bool{
    bool gameWon = false;

    if(character.position.col >= screenLength-8){
        gameWon = true;
    }

    return gameWon; 
}
//Some more awesome ascii art 
auto gameWonScreen(  unsigned int ticks ) -> void{
    MoveTo( screenWidth/2 -15, screenLength/2 -36);
    cout << "                                ,.        ,.      ,.                        " << "\n" << flush;
    cout << "                                ||        ||      ||  ()                    " << "\n"<< flush;
    cout << " ,--. ,-. ,.,-.  ,--.,.,-. ,-.  ||-.,.  ,.|| ,-.  ||-.,. ,-. ,.,-.  ,--.    " << "\n"<< flush;
    cout << "//`-'//-\\||/|| //-||||/`'//-\\ ||-'||  ||||//-\\ ||-'||//-\\||/|| ((`-'    " << "\n"<< flush;
    cout << "||   || |||| ||||  ||||   || || ||  || /|||||| || ||  |||| |||| ||  ``.     " << "\n"<< flush;
    cout << "\\,-.\\-//|| || \\-||||   \\-|| ||  ||//||||\\-|| ||  ||\\-//|| || ,-.))    " << "\n"<< flush;
    cout << " `--' `-' `' `'  `-,|`'    `-^-``'  `-' `'`' `-^-``'  `' `-' `' `' `--'     " << "\n"<< flush;
    cout << "                  //           .--------.                                   " << "\n"<< flush;
    cout << "              ,-.//          .: : :  :___`.                                 " << "\n"<< flush;
    cout << "              `--'         .'!!:::::  \\_| `.                               " << "\n"<< flush;
    cout << "                      : . /%O!!::::::::\\_|. |                              " << "\n"<< flush;
    cout << "                     [""]/%%O!!:::::::::  : . |                             " << "\n"<< flush;
    cout << "                     |  |%%OO!!::::::::::: : . |                            " << "\n"<< flush;
    cout << "                     |  |%%OO!!:::::::::::::  :|                            " << "\n"<< flush;
    cout << "                     |  |%%OO!!!::::::::::::: :|                            " << "\n"<< flush;
    cout << "            :       .'--`.%%OO!!!:::::::::::: :|                            " << "\n"<< flush;
    cout << "          : .:     /`.__.'|%%OO!!!::::::::::::/                             " << "\n"<< flush;
    cout << "         :    .   /        |%OO!!!!::::::::::/                              " << "\n"<< flush;
    cout << "        ,-'``'-. ;          ;%%OO!!!!!!:::::'                               " << "\n"<< flush;
    cout << "        |`-..-'| |   ,--.   |`%%%OO!!!!!!:'                                 " << "\n"<< flush;
    cout << "        | .   :| |_.','`.`._|  `%%%OO!%%'                                   " << "\n"<< flush;
    cout << "        | . :  | |--'    `--|    `%%%%'                                     " << "\n"<< flush;
    cout << "        |`-..-'| ||   | | | |     /__|`-.                                   " << "\n"<< flush;
    cout << "        |::::::/ ||)|/|)|)|||           /                                   " << "\n"<< flush;
    cout << "---------`::::'--|._ ~**~ _.|----------( -----------------------            " << "\n"<< flush;
    cout << "           )(    |  `-..-'  |           |    ______                         " << "\n"<< flush;
    cout << "           )(    |          |,--.       ____/ /  /\\ ,-._.-'                " << "\n"<< flush;
    cout << "        ,-')('-. |          ||`;/   .-()___  :  |`.!,-'`'/`-._              " << "\n"<< flush;
    cout << "       (  '  `  )`-._    _.-'|;,|    `-,    |_|__|`,-'>-.,-._               " << "\n"<< flush;
    cout << "        `-....-'     ````    `--'      `-._       (`- `-._`-.               " << "\n"<< flush;
     cout << "\033[1m\033[91m"
         << "Score: " << score << " Time: " << static_cast<float>(ticks / 10) << "s"
         << "\033[0m" << flush;
    
    endPosition();
}

auto main() -> int
{
    // Set Up the system to receive input
    SetupScreenAndInput();

    // Check that the terminal size is large enough for our fishies
    const position TERMINAL_SIZE{GetTerminalSize()};
    if ((TERMINAL_SIZE.row < 30) or (TERMINAL_SIZE.col < 100))
    {
        ShowCursor();
        TeardownScreenAndInput();
        cout << endl
             << "Terminal window must be at least 30 by 100 to run this game" << endl;
        return EXIT_FAILURE;
    }
    // State Variables
    unsigned int ticks{0};
    uniform_int_distribution<unsigned int> cloudgenerator(3, 8);
    uniform_int_distribution<unsigned int> chanceOfCloud(1, 10);
    unsigned int chanceForCloud;
    position screenSize = GetTerminalSize();
    screenWidth = screenSize.row;  //screenWidth;
    screenLength = screenSize.col; //screenLength
    position scoreposition{1, (screenLength / 2) - 18}; //set the position to the top center. The -18 is to center the text, otherwise the left side of the text would start at the middle
    bool collided1{false};
    bool collided2{false};
    bool collided3{false};

    player playercharacter{.position = {(screenWidth - 1), 0}};
    cloudvector clouds{}; //stores all of the clouds that will be generated and destroyed
    ground ground{.position = {(screenWidth), 0}}; //sets ground position to the bottom of the screen

    //generate anywhere from 3 to 8 clouds at the beginning
    for (unsigned int clouditerator = 0; clouditerator <= cloudgenerator(generator); clouditerator++)
    {
        cloud newCloud;
        clouds.push_back(newCloud);
    }

    // obstacle ob1{.position = {screenWidth - 3, obspawns(generator)}};
    // obstacle ob2{.position = {screenWidth - 3, obspawns(generator)}};
    // obstacle ob3{.position = {screenWidth - 3, obspawns(generator)}};

    //Again the function from the random class did not want to work with screenLength and screenWidth
    obstacle ob1{.position = {screenWidth - 3, experimental::randint(100, screenLength)}};
    obstacle ob2{.position = {screenWidth - 3, experimental::randint(100, screenLength)}};
    obstacle ob3{.position = {screenWidth - 3, experimental::randint(100, screenLength)}};

    char currentChar{};
    string currentCommand;

    bool allowBackgroundProcessing{true};
    bool showCommandline{false};

    auto startTimestamp{chrono::steady_clock::now()};
    auto endTimestamp{startTimestamp};
    int elapsedTimePerTick{100}; // Every 0.1s check on things
    SetNonblockingReadState(allowBackgroundProcessing);
    ClearScreen();
    HideCursor();

    while (currentChar != QUIT_CHAR)
    {
        // if(currentChar == BLOCKING_CHAR || currentChar == JUMP_CHAR || currentChar == EMPTY_CHAR){
        //     //A whole lotta nothin'
        // }
        
        // else
        // {
            //------------------------------------------------------------------------------------------------------------------------!MAGIC!-------------------------------------------------------------------------------------------------------------------------
            endTimestamp = chrono::steady_clock::now();
            auto elapsed{chrono::duration_cast<chrono::milliseconds>(endTimestamp - startTimestamp).count()};
            // We want to process input and update the world when EITHER
            // (a) there is background processing and enough time has elapsed
            // (b) when we are not allowing background processing.
            if (
                (allowBackgroundProcessing and (elapsed >= elapsedTimePerTick)) or (not allowBackgroundProcessing))
            {
                ticks++;
                cerr << "Ticks [" << ticks << "] allowBackgroundProcessing [" << allowBackgroundProcessing << "] elapsed [" << elapsed << "] currentChar [" << currentChar << "] currentCommand [" << currentCommand << "]" << endl;
                // if (currentChar == BLOCKING_CHAR) // Toggle background processing      
                // {

                //     else{
                //         allowBackgroundProcessing = not allowBackgroundProcessing;
                //         SetNonblockingReadState(allowBackgroundProcessing);
                //     }

                // }
                //------------------------------------------------------------------------------------------------------------------------!MAGIC!-------------------------------------------------------------------------------------------------------------------------
                ClearScreen();
                // The "actual" game. Draws the characters, and sets up new variables for the next ieration of the while loop.
                
                //make character jump
                if (currentChar == JUMP_CHAR or t > 0)
                {
                    jumpPlayer(playercharacter); 

                    if (currentChar == JUMP_CHAR and t == 0)
                    {
                        jumpPlayer(playercharacter); //Can jump immediately after touching the ground by calling it again
                    }
                }

                //This block generates a new cloud with a 1/10 chance every tick (0.1s) This means there should be a cloud roughly every second
                chanceForCloud = chanceOfCloud(generator);
                if (chanceForCloud == 1)
                {
                    cloud newCloud;
                    newCloud.position.col = screenLength - 1;
                    clouds.push_back(newCloud);
                }


                //each iteration the game checks if the player is colliding with the obstacles
                collided1 = checkCollision(ob1, playercharacter);             
                collided2 = checkCollision(ob2, playercharacter);
                collided3 = checkCollision(ob3, playercharacter);

                if (collided1 or collided2 or collided3)
                {
                    ShowCursor();
                    SetNonblockingReadState(false);
                    TeardownScreenAndInput();
                    // cout << endl; // be nice to the next command

                    ClearScreen();
                    gameOverScreen( ticks);


                    return EXIT_SUCCESS;
                }

                
                drawGround(ground);

                drawPlayer(playercharacter);

                drawClouds(clouds);
                moveClouds(clouds);

                drawObstacles(ob1);
                drawObstacles(ob2);
                drawObstacles(ob3);

                moveObstacles(ob1);
                moveObstacles(ob2);
                moveObstacles(ob3);

                drawScore( scoreposition, ticks );

        

                bool finished = checkWon(playercharacter);

                if(finished)
                {
                        ShowCursor();
                        SetNonblockingReadState(false);
                        TeardownScreenAndInput();
                        // cout << endl; // be nice to the next command

                        ClearScreen();
                        gameWonScreen( ticks);

                        return EXIT_SUCCESS;

                }

                // Clear inputs in preparation for the next iteration
                startTimestamp = endTimestamp;
                currentChar = NULL_CHAR;
                currentCommand.clear();
            }
            // Depending on the blocking mode, either read in one character or a string (character by character)
            if (showCommandline)
            {
                while (read(0, &currentChar, 1) == 1 && (currentChar != '\n'))
                {
                    cout << currentChar << flush; // the flush is important since we are in non-echoing mode
                    currentCommand += currentChar;
                }
                cerr << "Received command [" << currentCommand << "]" << endl;
                currentChar = NULL_CHAR;
            }
            else
            {
                read(0, &currentChar, 1);
            }
        }
        // Tidy Up and Close Down
        ShowCursor();
        SetNonblockingReadState(false);
        TeardownScreenAndInput();
        cout << endl; // be nice to the next command
        return EXIT_SUCCESS;

}
