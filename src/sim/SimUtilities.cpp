#include "SimUtilities.h"

#include <QCoreApplication>

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <ratio>
#include <fstream>
#include <glut/glut.h>
#include <iostream>
#include <iterator>
#include <sstream>
#include <QString>
#include <sys/stat.h>
#include <thread>
#include <random>

#ifdef _WIN32
    #include "Windows.h"
#else
    #include <dirent.h>
#endif

#include "Assert.h"
#include "Directory.h"
#include "Logging.h"
#include "Param.h"
#include "State.h"
#include "units/Seconds.h"

namespace sim {

void SimUtilities::quit() {
    // TODO: MACK - make a better API for exiting from each of the threads
    exit(1);
}

double SimUtilities::getRandom() {
    
    // The '- 1' ensures that the random number is never 1.
    // This matches the python implementation where random is [0,1).
    // This is particularly useful if you want to index into array like so
    // array[std::floor(random * <number of elements>)] without having to check
    // the condition if this function returns 1.
    
    static std::mt19937 generator(P()->randomSeed());
    return std::abs(static_cast<double>(generator()) - 1) / static_cast<double>(generator.max());
}

void SimUtilities::sleep(const Duration& duration) {
    int microseconds = static_cast<int>(std::floor(duration.getMicroseconds()));
    SIM_ASSERT_LE(0, microseconds);
	std::this_thread::sleep_for(std::chrono::microseconds(microseconds));
}

double SimUtilities::getHighResTimestamp() {
#ifdef _WIN32
    LARGE_INTEGER freq, counter;         // Keep windows queryperformance for the time being until tests can
    QueryPerformanceFrequency(&freq);    // be done. Supposedly up until Visual Studio 2013, microsoft uses
    QueryPerformanceCounter(&counter);   // the normal system clock for chrono high_resolution_clock which only
    return double(counter.QuadPart) / double(freq.QuadPart); // has 1 ms accuracy.
#else
    // chrono::high_resoltion_clock as defined by c++ 11 should use the highest resolution time source available
    // on the system.  See below for windows note.
    std::chrono::high_resolution_clock::time_point t1 = std::chrono::high_resolution_clock::now();
    // By default a cast to duration is a cast to seconds since the default ratio is 1
    return std::chrono::duration_cast<std::chrono::duration<double>>(t1.time_since_epoch()).count();
#endif
}

QString SimUtilities::timestampToDatetimeString(const Duration& timestamp) {
    time_t now = timestamp.getSeconds();
    struct tm tstruct = {0};
    tstruct = *gmtime(&now);
    char buf[80];
    strftime(buf, sizeof(buf), "%Y-%m-%d %X", &tstruct);
    return buf;
}

QString SimUtilities::formatSeconds(double seconds) {
    int minutes = 0;
    while (seconds >= 60) {
        minutes += 1;
        seconds -= 60;
    }
    QString secondsString = QString::number(seconds);
    if (secondsString.indexOf(".") < 2) {
        secondsString.insert(0, "0");
    }
    QString minutesString = QString::number(minutes);
    if (minutesString.size() < 2) {
        minutesString.insert(0, "0");
    }
    // We limit this to millisecond resolution
    return minutesString + ":" + secondsString.left(6);
}

bool SimUtilities::isBool(const QString& str) {
    return 0 == str.compare("true") || 0 == str.compare("false");
}

bool SimUtilities::isInt(const QString& str) {
    try {
        std::stoi(str.toStdString());
    }
    catch (...) {
        return false;
    }
    return true;
}

bool SimUtilities::isDouble(const QString& str) {
    try {
        std::stod(str.toStdString());
    }
    catch (...) {
        return false;
    }
    return true;
}

bool SimUtilities::strToBool(const QString& str) {
    SIM_ASSERT_TR(isBool(str));
    return 0 == str.compare("true");
}

int SimUtilities::strToInt(const QString& str) {
    SIM_ASSERT_TR(isInt(str));
    return std::stoi(str.toStdString());
}

double SimUtilities::strToDouble(const QString& str) {
    SIM_ASSERT_TR(isDouble(str));
    return std::stod(str.toStdString());
}

QVector<QString> SimUtilities::tokenize(
        const QString& str,
        char delimiter,
        bool ignoreEmpties,
        bool respectQuotes) {

    // TODO: upforgrabs
    // Replace this with some QT function

    QVector<QString> tokens;
    QString word = "";

    for (int i = 0; i < str.size(); i += 1) {
        if (respectQuotes && str.at(i) == '\"') {
            do {
                word += str.at(i);
                i += 1;
            } while (i < str.size() && str.at(i) != '\"');
        }
        if (str.at(i) == delimiter) {
            if (!word.isEmpty() || (0 < i && !ignoreEmpties)) {
                tokens.push_back(word);
                word = "";
            }
        }
        else {
            word += str.at(i);
        }
    }
    if (!word.isEmpty()) {
        tokens.push_back(word);
        word = "";
    }

    return tokens;
}

QString SimUtilities::trim(const QString& str) {
    return str.trimmed();
}

bool SimUtilities::isFile(const QString& path) {
    std::ifstream infile(path.toStdString());
    return infile.good();
}

QVector<QString> SimUtilities::getDirectoryContents(const QString& path) {

    QVector<QString> contents;

#ifdef _WIN32
    // TODO: upforgrabs
    // Implement a windows version of getDirectoryContents
#else
    // Taken from http://stackoverflow.com/a/612176/3176152
    DIR *dir = opendir(path.toStdString().c_str());
    if (dir != NULL) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            contents.push_back(path + QString(ent->d_name));
        }
        closedir(dir);
    }
#endif

    return contents;
}

void SimUtilities::removeExcessArchivedRuns() {
    // Information about each run is stored in the run/ directory. As it turns
    // out, this information can pile up pretty quickly. We should remove the
    // oldest stuff so that the run/ directory doesn't get too full.
    QVector<QString> contents = getDirectoryContents(Directory::getRunDirectory());
    std::sort(contents.begin(), contents.end());
    for (int i = 2; i < static_cast<int>(contents.size()) - P()->numberOfArchivedRuns(); i += 1) {
#ifdef _WIN32
        // TODO: upforgrabs
        // Implement a windows version of directory removal
#else
        system((QString("rm -rf \"") + contents.at(i) + QString("\"")).toStdString().c_str());
#endif
    }
}

} // namespace sim
