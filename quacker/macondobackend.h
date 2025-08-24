#ifndef MACONDO_BACKEND_H
#define MACONDO_BACKEND_H

#include <QProcess>
#include "game.h"

class QTimer;

struct MacondoInitOptions {
	inline MacondoInitOptions(std::string execPath) {
		this->execPath = execPath;
	}
	std::string execPath;
};
struct MacondoSimulateOptions {
	inline MacondoSimulateOptions() {}
};
struct MacondoEndgameOptions {
	inline MacondoEndgameOptions() {}
	bool firstWinOptimization = false;
	bool preventSlowRoll = false;
	int maxPlies = 15;
};
struct MacondoPreEndgameOptions {
	// if empty, we'll get Macondo to analyze all possible moves
	Quackle::MoveList movesToAnalyze;
	int endgamePlies = 4;
	bool skipNonEmptying = false;
	bool skipTieBreaker = false;
	bool earlyCutoff = false;
	std::string opponentRack;
	inline MacondoPreEndgameOptions() {}
};

class MacondoBackend: public QObject {
Q_OBJECT
public:
	MacondoBackend(Quackle::Game *game, const MacondoInitOptions &);
	~MacondoBackend();
	bool simulate(const MacondoSimulateOptions &options, const Quackle::MoveList &moves);
	void solveEndgame(const MacondoEndgameOptions &options);
	void solvePreEndgame(const MacondoPreEndgameOptions &options);
	std::string getSimResults();
	inline void setExecPath(const std::string &path) {
		m_execPath = path;
	}
	inline bool isRunning() const { return m_command != Command::None; }
	// stop current Macondo analysis
	void stop();
signals:
	void gotMoves(const Quackle::MoveList &moves);
	void statusMessage(const QString &message);
private slots:
	void processStarted();
	void processFinished(int, QProcess::ExitStatus);
	void timer();
private:
	Quackle::Move createPlaceMove(const std::string &placement, const std::string &tiles);
	Quackle::Move extractSimMove(const std::string &play);
	Quackle::MoveList extractSimMoves(QByteArray &processOutput);
	bool extractEndgameMove(QByteArray &processOutput, Quackle::Move &move);
	Quackle::Move extractPreEndgameMove(const std::string &moveStr);
	Quackle::MoveList extractPreEndgameMoves(const QByteArray &processOutput);
	enum class Command {
		None,
		Simulate,
		SolvePreEndgame,
		SolveEndgame,
	};
	bool startProcess();
	void loadGCG();
	void killProcess();
	void removeTempGCG();
	const char *updateDots(bool);
	std::string m_execPath;
	std::string m_tempGCG;
	QProcess *m_process = nullptr;
	QTimer *m_updateTimer = nullptr;
	int m_solveStatusDots = 3;
	int m_preEndgamePlaysToAnalyze = 0;
	int m_preEndgamePlaysAnalyzed = 0;
	// is simulation being run right now? (i.e. has process been started & game been loaded?)
	bool m_runningSimulation = false;
	Quackle::Game *m_game;
	QByteArray m_processOutput;
	QByteArray m_processStderr;
	Command m_command = Command::None;
	Quackle::MoveList m_movesToLoad;
	MacondoPreEndgameOptions m_preEndgameOptions;
	MacondoEndgameOptions m_endgameOptions;
};

#endif
