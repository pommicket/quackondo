#ifndef MACONDO_BACKEND_H
#define MACONDO_BACKEND_H

#include <QProcess>

namespace Quackle {
	class Game;
	class Move;
}

class QTimer;

class MacondoBackend: public QObject {
Q_OBJECT
public:
	struct InitOptions {
		inline InitOptions(std::string execPath) {
			this->execPath = execPath;
		}
		std::string execPath;
	};
	MacondoBackend(Quackle::Game *game, const InitOptions &);
	struct SimulateOptions {
		inline SimulateOptions() {}
	};
	void simulate(const SimulateOptions &);
	~MacondoBackend();
	std::string getSimResults();
signals:
	void gotSimMoves(const std::vector<Quackle::Move> &moves);
protected slots:
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
	bool m_runningSimulation = false;
	Quackle::Game *m_game;
	QByteArray m_processOutput;
	Command m_command = Command::None;
};

#endif
