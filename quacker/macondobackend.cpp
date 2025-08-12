#include "datamanager.h"
#include "macondobackend.h"
#include "quackleio/gcgio.h"
#include "game.h"

#include <QFile>
#include <QProcess>
#include <QTimer>
#include <QTextStream>
#include <random>

using std::string;
using std::vector;

static int getPlyNumber(const Quackle::GamePosition &position) {
	int playerIndex = 0, numPlayers = position.players().size();
	for (const Quackle::Player &player: position.players()) {
		if (player.id() == position.playerOnTurn().id()) {
			break;
		}
		playerIndex++;
	}
	if (playerIndex >= numPlayers) {
		throw "couldn't find player in player list";
	}
	return (position.turnNumber() - 1) * numPlayers
		+ playerIndex;
}

// locale-independent version of isspace
static bool is_ascii_space(char c) {
	return strchr(" \r\n\f\t\v", c) != nullptr;
}

static vector<string> split(const string &s) {
	vector<string> words;
	size_t i = 0;
	while (i < s.size()) {
		for (; is_ascii_space(s[i]); i++) {
			if (i >= s.size())
				return words;
		}
		size_t end = i + 1;
		for (; end < s.size() && !is_ascii_space(s[end]); end++);
		words.push_back(s.substr(i, end - i));
		i = end;
	}
	return words;
}

static string trimLeft(const string &s) {
	int i;
	for (i = 0; is_ascii_space(s[i]); i++);
	return s.substr(i);
}


MacondoBackend::MacondoBackend(Quackle::Game *game, const MacondoInitOptions &options) {
	m_execPath = options.execPath;
	m_game = game;
	m_updateTimer = new QTimer(this);
	m_updateTimer->setInterval(1000);
	connect(m_updateTimer, SIGNAL(timeout()), this, SLOT(timer()));
	m_updateTimer->start();
}

void MacondoBackend::simulate(const MacondoSimulateOptions &) {
	if (m_process) return;
	printf("running macondo %s\n", m_execPath.c_str());
	m_process = new QProcess(this);
	QStringList args;
	m_process->start(m_execPath.c_str(), args);
	connect(m_process, SIGNAL(started()), this, SLOT(processStarted()));
	connect(m_process, SIGNAL(finished(int, QProcess::ExitStatus)), this, SLOT(processFinished(int, QProcess::ExitStatus)));
	m_command = Command::Simulate;
}

static int parseScore(const string &scoreString) {
	size_t scoreLen;
	int score = 0;
	try {
		score = std::stoi(scoreString, &scoreLen);
		if (scoreLen != scoreString.length()) {
			qWarning("move score of '%s' is invalid", scoreString.c_str());
			score = 0;
		}
	} catch (const std::invalid_argument &) {
	} catch (const std::out_of_range &) {
	}
	return score;
}

static double parseWinRate(const string &winString) {
	size_t winLen;
	double win = 0.0;
	try {
		win = std::stod(winString, &winLen);
		if (winLen < winString.length() && winString.substr(winLen, strlen("±")) != string("±")) {
			qWarning("move win rate of '%s' is invalid", winString.c_str());
		}
	} catch (const std::invalid_argument &) {
	} catch (const std::out_of_range &) {
	}
	return win / 100;
}

static double parseEquity(const string &equityString) {
	size_t equityLen;
	double equity = 0.0;
	try {
		equity = std::stod(equityString, &equityLen);
		if (equityLen < equityString.length() && equityString.substr(equityLen, strlen("±")) != string("±")) {
			qWarning("move equality of '%s' is invalid", equityString.c_str());
		}
	} catch (const std::invalid_argument &) {
	} catch (const std::out_of_range &) {
	}
	return equity;
}

// parse move from Macondo sim
static Quackle::Move extractSimMove(const string &play) {
	vector<string> words = split(play);
	bool plays7 = words.size() == 5; // bingo/exchange 7 => no leave in output
	if (!plays7 && words.size() != 6) {
		throw "want 6 or 7 space-separated parts in play";
	}
	if (play.find("(Pass)") != string::npos) {
		if (!plays7) throw "want 6 space-separated parts in pass";
		const string &winString = words[3];
		const string &equityString = words[4];
		Quackle::Move move = Quackle::Move::createPassMove();
		move.win = parseWinRate(winString);
		move.equity = parseEquity(equityString);
		return move;
	} else if (play.find("(exch ") != string::npos) {
		// exchange
		string tiles = words[1];
		if (tiles[tiles.length() - 1] != ')')
			throw "expected ) following (exch";
		tiles.pop_back();
		const string &winString = words[3 + !plays7];
		const string &equityString = words[4 + !plays7];
		Quackle::Move move = Quackle::Move::createExchangeMove(QUACKLE_ALPHABET_PARAMETERS->encode(tiles), false);
		move.setPrettyTiles(QUACKLE_ALPHABET_PARAMETERS->encode(tiles));
		move.win = parseWinRate(winString);
		move.equity = parseEquity(equityString);
		return move;
	} else {
		// normal play
		const string &placement = words[0];
		const string &description = words[1];
		const string &scoreString = words[2 + !plays7];
		const string &winString = words[3 + !plays7];
		const string &equityString = words[4 + !plays7];
		string dotDescription;
		for (size_t i = 0; i < description.size();) {
			size_t j = description.find("(", i);
			if (j == string::npos) {
				dotDescription += description.substr(i);
				break;
			}
			dotDescription += description.substr(i, j) + ".";
			i = description.find(")", j);
			if (i == string::npos) throw "mismatched parentheses";
			i++;
		}
		Quackle::Move move = Quackle::Move::createPlaceMove(placement, QUACKLE_ALPHABET_PARAMETERS->encode(dotDescription));
		move.setPrettyTiles(QUACKLE_ALPHABET_PARAMETERS->encode(description));
		move.score = parseScore(scoreString);
		move.win = parseWinRate(winString);
		move.equity = parseEquity(equityString);
		return move;
	}
	throw "bad syntax";
}

// extract Quackle::Move from Macondo's sim output
static Quackle::MoveList extractSimMoves(QByteArray &processOutput) {
	Quackle::MoveList moves;
	QByteArray playsStartIdentifier("Play                Leave         Score    Win%            Equity");
	QByteArray playsEndIdentifier("Iterations:");
	int start = processOutput.indexOf(playsStartIdentifier) + playsStartIdentifier.length();
	if (start < 0) return moves;
	int end = processOutput.indexOf(playsEndIdentifier, start);
	if (end < 0) return moves;
	string plays(processOutput.constData() + start, end - start);
	processOutput.remove(0, end);
	plays = trimLeft(plays);
	for (size_t i = 0, next; (next = plays.find("\n", i)) != string::npos; i = next + 1) {
		string play = plays.substr(i, next - i);
		try {
			moves.push_back(extractSimMove(play));
		} catch (const char *s) {
			fprintf(stderr, "WARNING: unrecognized play: %s (%s)\n", play.c_str(), s);
		}
	}
	return moves;
}

void MacondoBackend::timer() {
	if (m_process) {
		QByteArray data = m_process->readAllStandardError();
		fprintf(stderr,"%s",data.constData());
		data = m_process->readAllStandardOutput();
		m_processOutput.append(data);
		fflush(stdout);
	}
	switch (m_command) {
	case Command::None:
		break;
	case Command::Simulate:
		if (m_runningSimulation) {
			m_process->write("sim show\n");
		}
		{
			Quackle::MoveList moves = extractSimMoves(m_processOutput);
			if (!moves.empty()) {
				// at this point the GCG is definitely fully loaded
				removeTempGCG();
				emit gotSimMoves(moves);
			}
		}
		break;
	case Command::Solve:
		// TODO
		break;
	}
}

void MacondoBackend::processStarted() {
	loadGCG();
	switch (m_command) {
	case Command::None:
		throw "process started with no command";
	case Command::Simulate:
		m_process->write("gen\nsim\n");
		m_runningSimulation = true;
		break;
	case Command::Solve:
		// TODO
		break;
	}
}

void MacondoBackend::loadGCG() {
	std::random_device randDev;
	std::mt19937 rand(randDev());
	std::uniform_int_distribution<int> distribution(0, 25);
	removeTempGCG();
	// save game file with random name
	char filename[] = "tmpGameXXXXXXXXXXXX.gcg";
	for (int i = 0; filename[i]; i++) {
		if (filename[i] == 'X') {
			filename[i] = distribution(rand) + 'A';
		}
	}
	m_tempGCG = filename;
	QuackleIO::GCGIO gcg;
	{
		QFile file(filename);
		file.open(QIODevice::WriteOnly | QIODevice::Text);
		QTextStream fileStream(&file);
		gcg.write(*m_game, fileStream);
	}
	std::stringstream commands;
	commands << "load " << filename << "\n"
		<< "turn " << getPlyNumber(m_game->currentPosition()) << "\n";
	m_process->write(commands.str().c_str());
}

void MacondoBackend::killProcess() {
	if (m_process) {
		m_process->kill();
		m_process->deleteLater();
		m_process = nullptr;
	}
}

void MacondoBackend::processFinished(int, QProcess::ExitStatus) {
}

// remove temporary GCG file if it still exists
void MacondoBackend::removeTempGCG() {
	if (!m_tempGCG.empty()) {
		remove(m_tempGCG.c_str());
		m_tempGCG.clear();
	}
}

MacondoBackend::~MacondoBackend() {
	if (m_process) {
		m_process->kill();
	}
}
