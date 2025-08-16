#include "macondobackend.h"
#include "datamanager.h"
#include "lexiconparameters.h"
#include "quackleio/gcgio.h"
#include "game.h"

#include <QFile>
#include <QProcess>
#include <QTimer>
#include <QTextStream>
#include <QThread>
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

static vector<string> splitLines(const string &s) {
	vector<string> lines;
	size_t i = 0;
	while (i < s.size()) {
		size_t end = s.find('\n', i);
		if (end == string::npos) end = s.size();
		if (end == i) {
			i++;
			continue;
		}
		string line = s.substr(i, end - i);
		lines.push_back(line);
		i = end + 1;
	}
	return lines;
}

static vector<string> splitWords(const string &s) {
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

void MacondoBackend::startProcess() {
	if (m_process) return;
	m_process = new QProcess(this);
	QStringList args;
	m_process->start(m_execPath.c_str(), args);
	connect(m_process, SIGNAL(started()), this, SLOT(processStarted()));
	connect(m_process, SIGNAL(finished(int, QProcess::ExitStatus)), this, SLOT(processFinished(int, QProcess::ExitStatus)));
}

void MacondoBackend::simulate(const MacondoSimulateOptions &options, const Quackle::MoveList &moves) {
	startProcess();
	m_movesToLoad = moves;
	m_command = Command::Simulate;
}

void MacondoBackend::solve(const MacondoSolveOptions &) {
	startProcess();
	bool isEndgame = m_game->currentPosition().unseenBag().size() <= 7;
	m_command = isEndgame ? Command::SolveEndgame : Command::SolvePreEndgame;
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

static Quackle::Move createPlaceMove(const string &placement, const string &prettyTiles) {
	string dotDescription;
	for (size_t i = 0; i < prettyTiles.size();) {
		size_t j = prettyTiles.find("(", i);
		if (j == string::npos) {
			dotDescription += prettyTiles.substr(i);
			break;
		}
		dotDescription += prettyTiles.substr(i, j) + ".";
		i = prettyTiles.find(")", j);
		if (i == string::npos) throw "mismatched parentheses";
		i++;
	}
	auto move = Quackle::Move::createPlaceMove(placement, QUACKLE_ALPHABET_PARAMETERS->encode(dotDescription));
	move.setPrettyTiles(QUACKLE_ALPHABET_PARAMETERS->encode(prettyTiles));
	return move;
}

// parse move from Macondo sim
static Quackle::Move extractSimMove(const string &play) {
	vector<string> words = splitWords(play);
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
		const string &prettyTiles = words[1];
		const string &scoreString = words[2 + !plays7];
		const string &winString = words[3 + !plays7];
		const string &equityString = words[4 + !plays7];
		Quackle::Move move = createPlaceMove(placement, prettyTiles);
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

static bool extractEndgameMove(QByteArray &processOutput, Quackle::Move &move) {
	QByteArray spreadDiffMarker("Best sequence has a spread difference (value) of ");
	if (!processOutput.contains(spreadDiffMarker)) {
		// delete all output except for the last line (which may be incomplete)
		int lastLineIndex = processOutput.lastIndexOf('\n');
		if (lastLineIndex >= 0) {
			QByteArray lastLine(processOutput.data() + lastLineIndex + 1,
				processOutput.size() - lastLineIndex - 1);
			processOutput = lastLine;
		}
		return false;
	}
	QByteArray bestSeqMarker("Best sequence:\n");
	int seqStart = processOutput.indexOf(bestSeqMarker);
	if (seqStart < 0) return false;
	seqStart += bestSeqMarker.length();
	string sequenceStr(processOutput.data() + seqStart, processOutput.size() - seqStart);
	vector<string> sequence = splitLines(sequenceStr);
	// TODO: do something with rest of moves? or just extract first move?
	const string &firstMove = sequence[0];
	vector<string> moveWords = splitWords(firstMove);
	if (moveWords.size() == 4) {
		const string &placement = moveWords[1];
		const string &prettyTiles = moveWords[2];
		const string &scoreInParentheses = moveWords[3];
		move = createPlaceMove(placement, prettyTiles);
		int score = 0;
		bool scoreIsValid = scoreInParentheses[0] == '(' && scoreInParentheses.back() == ')';
		try {
			size_t scoreLen = 0;
			score = std::stoi(scoreInParentheses.substr(1), &scoreLen);
			scoreIsValid = scoreLen == scoreInParentheses.length() - 2;
		} catch (const std::invalid_argument &) {
		} catch (const std::out_of_range &) {
		}
		if (scoreIsValid) {	
			move.score = score;
		} else {	
			qWarning("bad move score syntax: %s", scoreInParentheses.c_str());
		}
		processOutput.clear();
		return true;
	} else if (moveWords.size() == 3 && moveWords[1] == "Pass" && moveWords[2] == "(0)") {
		move = Quackle::Move::createPassMove();
		processOutput.clear();
		return true;
	} else {
		qWarning("bad move syntax: %s", firstMove.c_str());
	}
	processOutput.clear();
	return false;
}

void MacondoBackend::timer() {
	if (m_process) {
		QByteArray data = m_process->readAllStandardError();
		fprintf(stderr,"%.*s",data.size(), data.constData());
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
				emit gotMoves(moves);
			}
		}
		break;
	case Command::SolvePreEndgame:
		// TODO
		break;
	case Command::SolveEndgame:
		{
			if (m_processOutput.contains("Best sequence:")) {
				// give Macondo a bit more time to write out the full sequence
				QThread::msleep(60);
				m_processOutput.append(m_process->readAllStandardOutput());
			}
			Quackle::Move move;
			if (extractEndgameMove(m_processOutput, move)) {
				Quackle::MoveList list;
				list.push_back(move);
				printf("EMIT\n");
				emit gotMoves(list);
			}
		}
		break;
	}
}

void MacondoBackend::processStarted() {
	loadGCG();
	switch (m_command) {
	case Command::None:
		throw "process started with no command";
	case Command::Simulate:
		{
			std::stringstream commands;
			// add generated moves to Macondo
			for (const Quackle::Move &move: m_movesToLoad) {
				commands << "add ";
				switch (move.action) {
				case Quackle::Move::Action::Pass:
					commands << "pass";
					break;
				case Quackle::Move::Action::Exchange:
					commands << "exch ";
					commands << QUACKLE_ALPHABET_PARAMETERS->userVisible(move.tiles());
					break;
				case Quackle::Move::Action::Place:
				case Quackle::Move::Action::PlaceError:
					commands << move.positionString();
					commands << " ";
					commands << QUACKLE_ALPHABET_PARAMETERS->userVisible(move.prettyTiles());
					break;
				default:
					// ignore non-plays
					break;
				}
				commands << "\n";
			}
			
			commands << "sim\n";
			m_process->write(commands.str().c_str());
			m_runningSimulation = true;
		}
		break;
	case Command::SolvePreEndgame:
		// TODO
		break;
	case Command::SolveEndgame:
		m_process->write("endgame -plies 20\n");
		break;
	}
}

void MacondoBackend::loadGCG() {
	std::string lexicon = QUACKLE_LEXICON_PARAMETERS->lexiconName();
	for (size_t i = 0; i < lexicon.size(); i++) {
		if (lexicon[i] >= 'a' && lexicon[i] <= 'z') {
			lexicon[i] += 'A' - 'a';
		}
	}
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
	commands << "set lexicon " << lexicon << "\n"
		<< "load " << filename << "\n"
		<< "turn " << getPlyNumber(m_game->currentPosition()) << "\n";
	m_process->write(commands.str().c_str());
}

void MacondoBackend::killProcess() {
	if (m_process) {
		m_process->kill();
		// this is really unnecessary but prevents a
		// "process destroyed while running" warning message
		m_process->waitForFinished(100);
		delete m_process;
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
	killProcess();
	removeTempGCG();
}


void MacondoBackend::stop() {
	killProcess();
	removeTempGCG();
	m_runningSimulation = false;
	m_command = Command::None;
}
