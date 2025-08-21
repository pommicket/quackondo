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

// These "markers" are special parts of Macondo's standard output/error which we're looking for.
// We can change these whenever Macondo's output format changes.
static const QByteArray simPlaysStartMarker("Play                Leave         Score    Win%            Equity");
static const QByteArray simPlaysEndMarker("Iterations:");
static const QByteArray endgameSpreadDiffMarker("Best sequence has a spread difference (value) of ");
static const QByteArray endgameFinalSpreadMarker("Final spread after seq: ");
static const QByteArray endgameBestSeqMarker("Best sequence:\n");
// marks current ply depth during endgame solving
static const QByteArray endgamePlyMarker("\"ply\":");
// marks current best sequence during endgame solving
static const QByteArray endgameCurrBestMarker("\"pv\":\"");

static const QByteArray preEndgameStartingMarker("\"message\":\"starting to process ");
static const QByteArray preEndgameProgressMarker("\"message\":\"handled ");
static const QByteArray preEndgamePlaysStartMarker("Play                Wins    %Win    Spread   Outcomes");
static const QByteArray preEndgamePlaysEndMarker("❌ marks plays cut off early");

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


MacondoBackend::MacondoBackend(Quackle::Game *game, const MacondoInitOptions &options): QObject() {
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

void MacondoBackend::solveEndgame(const MacondoEndgameOptions &) {
	startProcess();
	m_command = Command::SolveEndgame;
}

void MacondoBackend::solvePreEndgame(const MacondoPreEndgameOptions &) {
	startProcess();
	m_command = Command::SolvePreEndgame;
	m_preEndgamePlaysToAnalyze = 0;
}

static bool parseInt(const string &s, int &value, size_t *len) {
	try {
		value = std::stoi(s, len);
		return true;
	} catch (const std::invalid_argument &) {
		return false;
	} catch (const std::out_of_range &) {
		return false;
	}
	
}


static int parseScore(const string &scoreString) {
	size_t scoreLen;
	int score = 0;
	if (parseInt(scoreString, score, &scoreLen)) {
		if (scoreLen != scoreString.length()) {
			qWarning("move score of '%s' is invalid", scoreString.c_str());
			score = 0;
		}
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
	int start = processOutput.indexOf(simPlaysStartMarker) + simPlaysStartMarker.length();
	if (start < 0) return moves;
	int end = processOutput.indexOf(simPlaysEndMarker, start);
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
	int spreadDiffMarkerIdx = processOutput.indexOf(endgameSpreadDiffMarker);
	if (spreadDiffMarkerIdx < 0) {
		// delete all output except for the last line (which may be incomplete)
		int lastLineIndex = processOutput.lastIndexOf('\n');
		if (lastLineIndex >= 0) {
			QByteArray lastLine(processOutput.data() + lastLineIndex + 1,
				processOutput.size() - lastLineIndex - 1);
			processOutput = lastLine;
		}
		return false;
	}
	int spreadDiffStart = spreadDiffMarkerIdx + endgameSpreadDiffMarker.length();
	int spreadDiffEnd = processOutput.indexOf("\n", spreadDiffStart);
	if (spreadDiffEnd < 0) return false;
	int spreadDiff = -999;
	string spreadDiffStr(processOutput.data() + spreadDiffStart, spreadDiffEnd - spreadDiffStart);
	if (!parseInt(spreadDiffStr, spreadDiff, nullptr)) {
		qWarning("bad sequence spread: %s", spreadDiffStr.c_str());
	}
	int finalSpread = INT_MIN;
	int finalSpreadMarkerIdx = processOutput.indexOf(endgameFinalSpreadMarker);
	if (finalSpreadMarkerIdx < 0) return false;
	int finalSpreadStart = finalSpreadMarkerIdx + endgameFinalSpreadMarker.length();
	int finalSpreadEnd = processOutput.indexOf("\n", finalSpreadStart);
	std::string finalSpreadStr(processOutput.data() + finalSpreadStart, finalSpreadEnd - finalSpreadStart);
	if (!parseInt(finalSpreadStr, finalSpread, nullptr)) {
		qWarning("bad final spread: %s", finalSpreadStr.c_str());
	}
	int seqStart = processOutput.indexOf(endgameBestSeqMarker);
	if (seqStart < 0) return false;
	seqStart += endgameBestSeqMarker.length();
	string sequenceStr(processOutput.data() + seqStart, processOutput.size() - seqStart);
	vector<string> sequence = splitLines(sequenceStr);
	const string &firstMove = sequence[0];
	vector<string> moveWords = splitWords(firstMove);
	bool ok = false;
	if (moveWords.size() == 4) {
		const string &placement = moveWords[1];
		const string &prettyTiles = moveWords[2];
		const string &scoreInParentheses = moveWords[3];
		move = createPlaceMove(placement, prettyTiles);
		int score = 0;
		bool scoreIsValid = scoreInParentheses[0] == '(' && scoreInParentheses.back() == ')';
		size_t scoreLen = 0;
		if (parseInt(scoreInParentheses.substr(1), score, &scoreLen)) {
			scoreIsValid = scoreLen == scoreInParentheses.length() - 2;
		}
		if (scoreIsValid) {
			move.score = score;
		} else {
			qWarning("bad move score syntax: %s", scoreInParentheses.c_str());
		}
		ok = true;
	} else if (moveWords.size() == 3 && moveWords[1] == "Pass" && moveWords[2] == "(0)") {
		move = Quackle::Move::createPassMove();
		ok = true;
	} else {
		qWarning("bad move syntax: %s", firstMove.c_str());
	}
	processOutput.clear();
	if (ok) {
		std::stringstream remainingSequence;
		// show remainder of sequence as comment on Move
		for (size_t i = 1; i < sequence.size(); i++) {
			if (i > 1) remainingSequence << " ";
			remainingSequence << sequence[i];
		}
		move.equity = spreadDiff;
		move.comment = remainingSequence.str();
		move.win = finalSpread < 0 ? 0.0 // loss/something went wrong
			: finalSpread == 0 ? 0.5 // draw
			: 1.0; // win
	}
	return ok;
}

static Quackle::Move extractPreEndgameMove(const string &moveStr) {
	size_t outcomesStart = std::min(moveStr.find("👍"), std::min(moveStr.find("👎"), moveStr.find("🤝")));
	if (outcomesStart == string::npos) {
		throw "expected 👍/👎/🤝 to mark start of outcomes";
	}
	string outcomes = moveStr.substr(outcomesStart);
	vector<string> words = splitWords(moveStr.substr(0, outcomesStart));
	if (words.size() < 3) throw "expected at least 3 space-separated parts before outcome";
	Quackle::Move move;
	if (words[0] == "(Pass)") {
		words.erase(words.begin());
		move = Quackle::Move::createPassMove();
	} else if (words[0] == "(exch") { // probably Macondo will never be able to solve 7-in-the-bag but who knows
		string tiles = words[1];
		if (tiles[tiles.length() - 1] != ')')
			throw "expected ) following (exch";
		tiles.pop_back();
		move = Quackle::Move::createExchangeMove(QUACKLE_ALPHABET_PARAMETERS->encode(tiles), false);
		words.erase(words.begin(), words.begin() + 2);
	} else {
		move = createPlaceMove(words[0], words[1]);
		words.erase(words.begin(), words.begin() + 2);
	}
	if (words.size() < 2) throw "missing wins/win%";
	move.win = parseWinRate(words[1]);
	// looks like equity is only computed for plays that are in the lead and tied
	if (words.size() >= 3)
		move.equity = parseEquity(words[2]);
	else
		move.equity = -999 + 0.01 * move.win; // ensure moves are still sorted by win%
	move.comment = outcomes;
	return move;
}

static Quackle::MoveList extractPreEndgameMoves(const QByteArray &processOutput) {
	Quackle::MoveList list;
	int startMarkerIdx = processOutput.indexOf(preEndgamePlaysStartMarker);
	if (startMarkerIdx == -1) return list;
	int startIdx = processOutput.indexOf('\n', startMarkerIdx + preEndgamePlaysStartMarker.length()) + 1;
	if (startIdx == 0) return list;
	int endIdx = processOutput.indexOf(preEndgamePlaysEndMarker, startIdx);
	if (endIdx == -1) return list;
	vector<string> moves = splitLines(string(processOutput.constData() + startIdx, endIdx - startIdx));
	for (const string &moveStr: moves) {
		try {
			list.push_back(extractPreEndgameMove(moveStr));
		} catch (const char *err) {
			fprintf(stderr, "WARNING: bad syntax in pre-endgame move string (%s): %s",
				err, moveStr.c_str());
		}
	}
	Quackle::MoveList::sort(list);
	return list;
}

const char *MacondoBackend::updateDots(bool anythingNew) {
	const char *dots = m_solveStatusDots == 3 ? "..."
		: m_solveStatusDots == 4 ? "...."
		: ".....";
	m_solveStatusDots += anythingNew;
	if (m_solveStatusDots == 6) m_solveStatusDots = 3;
	return dots;
}

void MacondoBackend::timer() {
	bool anyNewOutput = false;
	if (m_process) {
		QByteArray data = m_process->readAllStandardError();
		anyNewOutput |= data.size() != 0;
		fprintf(stderr,"%.*s",data.size(), data.constData());
		m_processStderr.append(data);
		data = m_process->readAllStandardOutput();
		anyNewOutput |= data.size() != 0;
		m_processOutput.append(data);
		fflush(stdout);
	}
	const char *dots = updateDots(anyNewOutput);
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
		if (m_processOutput.contains(preEndgamePlaysEndMarker)) {
			removeTempGCG();
			Quackle::MoveList moves = extractPreEndgameMoves(m_processOutput);
			if (!moves.empty()) {
				Quackle::GamePosition &position = m_game->currentPosition();
				for (Quackle::Move &move: moves) {
					position.scoreMove(move);
				}
				// at this point the GCG is definitely fully loaded
				removeTempGCG();
				emit gotMoves(moves);
			}
			break;
		}
		if (int i = m_processStderr.lastIndexOf(preEndgameStartingMarker); i != -1
			&& m_processStderr.indexOf('\n', i) != -1) {
			removeTempGCG();
			// extract # of plays Macondo will analyze
			i += preEndgameStartingMarker.length();
			m_processStderr = QByteArray(m_processStderr.constData() + i, m_processStderr.size() - i);
			string numPlaysStr(m_processStderr.constData(), std::min(32, m_processStderr.size()));
			int numPlays = 0;
			parseInt(numPlaysStr, numPlays, nullptr);
			m_preEndgamePlaysToAnalyze = numPlays;
			std::stringstream message;
			message << "Analyzed 0/" << m_preEndgamePlaysToAnalyze << " plays" << dots;
			emit statusMessage(QString::fromStdString(message.str()));
		}
		if (m_preEndgamePlaysToAnalyze) {
			int i = m_processStderr.lastIndexOf(preEndgameProgressMarker);
			// extract # of plays Macondo has analyzed so far
			if (i != -1 && m_processStderr.indexOf('\n', i) != -1) {
				i += preEndgameProgressMarker.length();
				m_processStderr = QByteArray(m_processStderr.constData() + i, m_processStderr.size() - i);
				string numPlaysStr(m_processStderr.constData(), std::min(32, m_processStderr.size()));
				int numPlays = 0;
				parseInt(numPlaysStr, numPlays, nullptr);
				std::stringstream message;
				message << "Analyzed " << numPlays << "/" << m_preEndgamePlaysToAnalyze << " plays" << dots;
				emit statusMessage(QString::fromStdString(message.str()));
			}
		} else {
			// no progress yet
			emit statusMessage(QString("Solving pre-endgame") + dots);
		}
		break;
	case Command::SolveEndgame:
		if (m_processOutput.contains(endgameBestSeqMarker)) {
			removeTempGCG();
			// give Macondo a bit more time to write out the full sequence
			QThread::msleep(60);
			m_processOutput.append(m_process->readAllStandardOutput());
			Quackle::Move move;
			if (extractEndgameMove(m_processOutput, move)) {
				Quackle::MoveList list;
				list.push_back(move);
				emit gotMoves(list);
			}
		} else if (anyNewOutput) {
			// update status
			int lastNewline = m_processStderr.lastIndexOf('\n');
			if (lastNewline == -1) return;
			// extract current ply number from stderr
			int plyMarkerIdx = m_processStderr.lastIndexOf(endgamePlyMarker, lastNewline);
			int ply = 0;
			if (plyMarkerIdx != -1) {
				int plyStart = plyMarkerIdx + endgamePlyMarker.length();
				std::string plyStr(m_processStderr.constData() + plyStart,
					std::min(m_processStderr.length() - plyStart, 30));
				parseInt(plyStr, ply, nullptr);
			}
			// extract current best sequence
			int currBestMarkerIdx = m_processStderr.lastIndexOf(endgameCurrBestMarker, lastNewline);
			std::string currBest;
			if (currBestMarkerIdx != -1) {
				removeTempGCG();
				int currBestStart = currBestMarkerIdx + endgameCurrBestMarker.length();
				int currBestEnd = m_processStderr.indexOf('"', currBestStart);
				if (currBestEnd != -1) {
					currBest = std::string(m_processStderr.constData() + currBestStart,
						currBestEnd - currBestStart);
				}
			}
			std::stringstream status;
			status << "Solving endgame";
			if (ply) {
				status << " (" << ply << " plies deep)";
			}
			if (!currBest.empty()) {
				status << ", current best: " << currBest;
			}
			status << dots;
			emit statusMessage(QString::fromStdString(status.str()));
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
		m_process->write("peg -disable-id true -early-cutoff true\n");
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
	m_processOutput.clear();
	m_processStderr.clear();
	emit statusMessage("");
}
