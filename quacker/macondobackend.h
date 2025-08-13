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

class MacondoBackend: public QObject {
Q_OBJECT
public:
	MacondoBackend(Quackle::Game *game, const MacondoInitOptions &);
	void simulate(const MacondoSimulateOptions &options, const Quackle::MoveList &moves);
	~MacondoBackend();
	std::string getSimResults();
	inline bool isRunning() const { return m_command != Command::None; }
	// stop current Macondo analysis
	void stop();
signals:
	void gotSimMoves(const Quackle::MoveList &moves);
private slots:
	void processStarted();
	void processFinished(int, QProcess::ExitStatus);
	void timer();
private:
	enum class Command {
		None,
		Simulate,
		Solve,
	};
	void loadGCG();
	void killProcess();
	void removeTempGCG();
	std::string m_execPath;
	std::string m_tempGCG;
	QProcess *m_process = nullptr;
	QTimer *m_updateTimer = nullptr;
	// is simulation being run right now? (i.e. has process been started & game been loaded?)
	bool m_runningSimulation = false;
	Quackle::Game *m_game;
	QByteArray m_processOutput;
	Command m_command = Command::None;
	Quackle::MoveList m_movesToLoad;
};

#endif
