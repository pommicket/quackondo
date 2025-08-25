#include "macondobackend.h"
#include "datamanager.h"
#include "lexiconparameters.h"
#include "quackleio/gcgio.h"
#include "game.h"

#include <QFile>
#include <QOperatingSystemVersion>
#include <QProcess>
#include <QTimer>
#include <QTextStream>
#include <QThread>
#include <QCoreApplication>
#include <random>
#include <climits>

static bool isWindows() {
	return QOperatingSystemVersion::currentType() == QOperatingSystemVersion::Windows;
}

// These "markers" are special parts of Macondo's standard output/error which we're looking for.
// We can change these whenever Macondo's output format changes.
static const QByteArray simPlaysStartMarker("Play                Leave         Score    Win%            Equity");
static const QByteArray simPlaysEndMarker("Iterations:");
static const QByteArray endgameSpreadDiffMarker1("Best sequence has a spread difference (value) of ");
static const QByteArray endgameSpreadDiffMarker2("Spread diff: "); // this is used if first-win-optim is enabled
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

static std::vector<std::string> splitLines(const std::string &s) {
	std::vector<std::string> lines;
	size_t i = 0;
	while (i < s.size()) {
		size_t end = s.find('\n', i);
		if (end == std::string::npos) end = s.size();
		if (end == i) {
			i++;
			continue;
		}
		std::string line = s.substr(i, end - i);
		lines.push_back(line);
		i = end + 1;
	}
	return lines;
}

static std::vector<std::string> splitWords(const std::string &s) {
	std::vector<std::string> words;
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

static std::string trimLeft(const std::string &s) {
	int i;
	for (i = 0; is_ascii_space(s[i]); i++);
	return s.substr(i);
}

// avoid annoying ambiguous template parameter issues from std::min
static size_t minSize(size_t a, size_t b) {
	return std::min(a, b);
}

MacondoBackend::MacondoBackend(Quackle::Game *game, const MacondoInitOptions &options): QObject() {
	m_execPath = options.execPath;
	m_game = game;
	m_updateTimer = new QTimer(this);
	m_updateTimer->setInterval(1000);
	connect(m_updateTimer, SIGNAL(timeout()), this, SLOT(timer()));
	m_updateTimer->start();
}

bool MacondoBackend::startProcess(Command command) {
	if (m_process) return true;
	m_command = command;
	m_process = new QProcess(this);
	QStringList args;
	connect(m_process, SIGNAL(started()), this, SLOT(processStarted()));
	connect(m_process, SIGNAL(finished(int, QProcess::ExitStatus)), this, SLOT(processFinished(int, QProcess::ExitStatus)));
	m_process->start(m_execPath.c_str(), args, QIODevice::Text | QIODevice::ReadWrite);
	return true;
}

bool MacondoBackend::simulate(const MacondoSimulateOptions &options, const Quackle::MoveList &moves) {
	m_movesToLoad = moves;
	if (!startProcess(Command::Simulate))
		return false;
	return true;
}

void MacondoBackend::solveEndgame(const MacondoEndgameOptions &options) {
	m_endgameOptions = options;
	startProcess(Command::SolveEndgame);
}

void MacondoBackend::solvePreEndgame(const MacondoPreEndgameOptions &options) {
	m_preEndgamePlaysToAnalyze = 0;
	m_preEndgameOptions = options;
	startProcess(Command::SolvePreEndgame);
}

static bool parseInt(const std::string &s, int &value, size_t *len) {
	try {
		value = std::stoi(s, len);
		return true;
	} catch (const std::invalid_argument &) {
		return false;
	} catch (const std::out_of_range &) {
		return false;
	}
	
}


static int parseScore(const std::string &scoreString) {
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

static double parseWinRate(const std::string &winString) {
	size_t winLen;
	double win = 0.0;
	try {
		win = std::stod(winString, &winLen);
		if (winLen < winString.length() && winString.substr(winLen, strlen("±")) != std::string("±")) {
			qWarning("move win rate of '%s' is invalid", winString.c_str());
		}
	} catch (const std::invalid_argument &) {
	} catch (const std::out_of_range &) {
	}
	return win / 100;
}

static double parseEquity(const std::string &equityString) {
	size_t equityLen;
	double equity = 0.0;
	try {
		equity = std::stod(equityString, &equityLen);
		if (equityLen < equityString.length() && equityString.substr(equityLen, strlen("±")) != std::string("±")) {
			qWarning("move equality of '%s' is invalid", equityString.c_str());
		}
	} catch (const std::invalid_argument &) {
	} catch (const std::out_of_range &) {
	}
	return equity;
}

Quackle::Move MacondoBackend::createPlaceMove(const std::string &placement, const std::string &tiles) {
	std::string dotDescription;
	// convert pretty tiles to dot description if necessary
	for (size_t i = 0; i < tiles.size();) {
		size_t j = tiles.find('(', i);
		if (j == std::string::npos) {
			dotDescription += tiles.substr(i);
			break;
		}
		dotDescription += tiles.substr(i, j - i);
		i = tiles.find(')', j);
		if (i == std::string::npos) throw "mismatched parentheses";
		// add appropriate number of dots
		for (size_t d = 1; d < i - j; d++)
			dotDescription.push_back('.');
		i++;
	}
	auto move = Quackle::Move::createPlaceMove(placement, QUACKLE_ALPHABET_PARAMETERS->encode(dotDescription));
	move.setPrettyTiles(m_game->currentPosition().board().prettyTilesOfMove(move));
	return move;
}

// parse move from Macondo sim
Quackle::Move MacondoBackend::extractSimMove(const std::string &play) {
	std::vector<std::string> words = splitWords(play);
	bool plays7 = words.size() == 5; // bingo/exchange 7 => no leave in output
	if (!plays7 && words.size() != 6) {
		throw "want 6 or 7 space-separated parts in play";
	}
	if (play.find("(Pass)") != std::string::npos) {
		if (!plays7) throw "want 6 space-separated parts in pass";
		const std::string &winString = words[3];
		const std::string &equityString = words[4];
		Quackle::Move move = Quackle::Move::createPassMove();
		move.win = parseWinRate(winString);
		move.equity = parseEquity(equityString);
		return move;
	} else if (play.find("(exch ") != std::string::npos) {
		// exchange
		std::string tiles = words[1];
		if (tiles[tiles.length() - 1] != ')')
			throw "expected ) following (exch";
		tiles.pop_back();
		const std::string &winString = words[3 + !plays7];
		const std::string &equityString = words[4 + !plays7];
		Quackle::Move move = Quackle::Move::createExchangeMove(QUACKLE_ALPHABET_PARAMETERS->encode(tiles), false);
		move.setPrettyTiles(QUACKLE_ALPHABET_PARAMETERS->encode(tiles));
		move.win = parseWinRate(winString);
		move.equity = parseEquity(equityString);
		return move;
	} else {
		// normal play
		const std::string &placement = words[0];
		const std::string &tiles = words[1];
		const std::string &scoreString = words[2 + !plays7];
		const std::string &winString = words[3 + !plays7];
		const std::string &equityString = words[4 + !plays7];
		Quackle::Move move = createPlaceMove(placement, tiles);
		move.score = parseScore(scoreString);
		move.win = parseWinRate(winString);
		move.equity = parseEquity(equityString);
		return move;
	}
	throw "bad syntax";
}

// extract Quackle::Move from Macondo's sim output
Quackle::MoveList MacondoBackend::extractSimMoves(QByteArray &processOutput) {
	Quackle::MoveList moves;
	int start = processOutput.indexOf(simPlaysStartMarker) + simPlaysStartMarker.length();
	if (start < 0) return moves;
	int end = processOutput.indexOf(simPlaysEndMarker, start);
	if (end < 0) return moves;
	std::string plays(processOutput.constData() + start, end - start);
	processOutput.remove(0, end);
	plays = trimLeft(plays);
	for (size_t i = 0, next; (next = plays.find("\n", i)) != std::string::npos; i = next + 1) {
		std::string play = plays.substr(i, next - i);
		try {
			moves.push_back(extractSimMove(play));
		} catch (const char *s) {
			fprintf(stderr, "WARNING: unrecognized play: %s (%s)\n", play.c_str(), s);
		}
	}
	return moves;
}

bool MacondoBackend::extractEndgameMove(QByteArray &processOutput, Quackle::Move &move) {
	const QByteArray &spreadDiffMarker = processOutput.contains(endgameSpreadDiffMarker1)
		? endgameSpreadDiffMarker1 : endgameSpreadDiffMarker2;
	int spreadDiffMarkerIdx = processOutput.indexOf(spreadDiffMarker);
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
	int spreadDiffStart = spreadDiffMarkerIdx + spreadDiffMarker.length();
	int spreadDiffEnd = processOutput.indexOf("\n", spreadDiffStart);
	if (spreadDiffEnd < 0) return false;
	int spreadDiff = -999;
	std::string spreadDiffStr(processOutput.data() + spreadDiffStart, spreadDiffEnd - spreadDiffStart);
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
	std::string sequenceStr(processOutput.data() + seqStart, processOutput.size() - seqStart);
	std::vector<std::string> sequence = splitLines(sequenceStr);
	const std::string &firstMove = sequence[0];
	std::vector<std::string> moveWords = splitWords(firstMove);
	bool ok = false;
	if (moveWords.size() == 4) {
		const std::string &placement = moveWords[1];
		const std::string &prettyTiles = moveWords[2];
		const std::string &scoreInParentheses = moveWords[3];
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

Quackle::Move MacondoBackend::extractPreEndgameMove(const std::string &moveStr) {
	size_t outcomesStart = minSize(moveStr.find("👍"), minSize(moveStr.find("👎"), moveStr.find("🤝")));
	if (outcomesStart == std::string::npos) {
		throw "expected 👍/👎/🤝 to mark start of outcomes";
	}
	std::string outcomes = moveStr.substr(outcomesStart);
	std::vector<std::string> words = splitWords(moveStr.substr(0, outcomesStart));
	if (words.size() < 3) throw "expected at least 3 space-separated parts before outcome";
	Quackle::Move move;
	if (words[0] == "(Pass)") {
		words.erase(words.begin());
		move = Quackle::Move::createPassMove();
	} else if (words[0] == "(exch") { // probably Macondo will never be able to solve 7-in-the-bag but who knows
		std::string tiles = words[1];
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

Quackle::MoveList MacondoBackend::extractPreEndgameMoves(const QByteArray &processOutput) {
	Quackle::MoveList list;
	int startMarkerIdx = processOutput.indexOf(preEndgamePlaysStartMarker);
	if (startMarkerIdx == -1) return list;
	int startIdx = processOutput.indexOf('\n', startMarkerIdx + preEndgamePlaysStartMarker.length()) + 1;
	if (startIdx == 0) return list;
	int endIdx = processOutput.indexOf(preEndgamePlaysEndMarker, startIdx);
	if (endIdx == -1) return list;
	std::vector<std::string> moves = splitLines(std::string(processOutput.constData() + startIdx, endIdx - startIdx));
	for (const std::string &moveStr: moves) {
		try {
			list.push_back(extractPreEndgameMove(moveStr));
		} catch (const char *err) {
			fprintf(stderr, "WARNING: bad syntax in pre-endgame move std::string (%s): %s",
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


void MacondoBackend::send(const QByteArray &data) {
	if (data.isEmpty()) return;
	emit newLogOutput(data);
	m_process->write(data);
}

QByteArray MacondoBackend::receiveStdout() {
	return m_process->readAllStandardOutput();
}

QByteArray MacondoBackend::receiveStderr() {
	return m_process->readAllStandardError();
}

void MacondoBackend::timer() {
	bool anyNewOutput = false;
	if (m_process) {
		QByteArray data = receiveStderr();
		if (data.size()) {
			anyNewOutput = true;
			emit newLogOutput(data);
		}
		//fprintf(stderr,"%.*s",data.size(), data.constData());
		m_processStderr.append(data);
		data = receiveStdout();
		m_processOutput.append(data);
		if (data.size()) {
			anyNewOutput = true;
			emit newLogOutput(data);
		}
		fflush(stdout);
	}
	const char *dots = updateDots(anyNewOutput);
	switch (m_command) {
	case Command::None:
		break;
	case Command::Simulate:
		if (m_runningSimulation) {
			send("sim show\n");
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
			std::string numPlaysStr(m_processStderr.constData(), minSize(32, m_processStderr.size()));
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
				std::string numPlaysStr(m_processStderr.constData(), minSize(32, m_processStderr.size()));
				parseInt(numPlaysStr, m_preEndgamePlaysAnalyzed, nullptr);
			}
			std::stringstream message;
			message << "Analyzed " << m_preEndgamePlaysAnalyzed << "/" << m_preEndgamePlaysToAnalyze << " plays" << dots;
			emit statusMessage(QString::fromStdString(message.str()));		
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
			m_processOutput.append(receiveStdout());
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
					minSize(m_processStderr.length() - plyStart, 30));
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

static std::string moveToString(const Quackle::Move &move) {
	switch (move.action) {
	case Quackle::Move::Action::Pass:
		return "pass";
	case Quackle::Move::Action::Exchange:
		return std::string("exch ") + QUACKLE_ALPHABET_PARAMETERS->userVisible(move.tiles());
	case Quackle::Move::Action::Place:
	case Quackle::Move::Action::PlaceError:
		return move.positionString() + " " + QUACKLE_ALPHABET_PARAMETERS->userVisible(move.tiles());
	default:
		// blind exchanges, etc.
		return "";
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
				std::string moveStr = moveToString(move);
				if (!moveStr.empty()) {
					commands << "add ";
					commands << moveStr;
					commands << "\n";
				}
			}
			
			commands << "sim\n";
			send(commands.str().c_str());
			m_runningSimulation = true;
		}
		break;
	case Command::SolvePreEndgame:
		{
			std::stringstream command;
			command << "peg ";
			for (const Quackle::Move &move: m_preEndgameOptions.movesToAnalyze) {
				std::string moveStr = moveToString(move);
				if (!moveStr.empty()) {
					command << "-only-solve \"" << moveStr << "\" ";
				}
			}
			command << "-endgameplies " << m_preEndgameOptions.endgamePlies << " ";
			command << "-disable-id true ";
			command << "-early-cutoff " << (m_preEndgameOptions.earlyCutoff ? "true" : "false") << " ";
			command << "-skip-nonemptying " << (m_preEndgameOptions.skipNonEmptying ? "true" : "false") << " ";
			command << "-skip-tiebreaker " << (m_preEndgameOptions.skipTieBreaker ? "true" : "false") << " ";
			if (!m_preEndgameOptions.opponentRack.empty()) {
				command << "-opprack " << m_preEndgameOptions.opponentRack << " ";
			}
			command << "\n";
			send(command.str().c_str());
		}
		break;
	case Command::SolveEndgame:
		{
			std::stringstream command;
			command << "endgame ";
			command << "-first-win-optim " << (m_endgameOptions.firstWinOptimization ? "true" : "false") << " ";
			command << "-prevent-slowroll " << (m_endgameOptions.preventSlowRoll ? "true" : "false") << " ";
			command << "-plies " << m_endgameOptions.maxPlies << " ";
			command << "\n";
			send(command.str().c_str());
		}
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
	std::string filename = (QCoreApplication::applicationDirPath() + "/tmpGameXXXXXXXXXXXX.gcg").toStdString();
	for (size_t i = 0; i < filename.length(); i++) {
		if (filename[i] == 'X') {
			filename[i] = distribution(rand) + 'A';
		}
	}
	m_tempGCG = filename;
	QuackleIO::GCGIO gcg;
	{
		QFile file(filename.c_str());
		file.open(QIODevice::WriteOnly | QIODevice::Text);
		QTextStream fileStream(&file);
		gcg.write(*m_game, fileStream);
	}
	std::stringstream commands;
	commands << "set lexicon " << lexicon << "\n"
		<< "load '" << filename << "'\n"
		<< "turn " << getPlyNumber(m_game->currentPosition()) << "\n";
	send(commands.str().c_str());
}

void MacondoBackend::killProcess() {
	if (m_process) {
		m_process->kill();
		// this is really unnecessary but prevents a
		// "process destroyed while running" warning message
		m_process->waitForFinished(200);
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
