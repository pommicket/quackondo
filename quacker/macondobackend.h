#ifndef MACONDO_BACKEND_H_
#define MACONDO_BACKEND_H_

#include <QObject>
#include <QProcess>

namespace Quackle {
	class Game;
}

enum class MacondoCommand {
	None,
	Simulate,
	Solve,
};

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
	inline MacondoCommand command() const { return m_command; }
protected slots:
	void processStarted();
	void processFinished(int, QProcess::ExitStatus);
private:
	void loadGCG();
	void killProcess();
	std::string m_execPath;
	std::string m_tempGCG;
	QProcess *m_process = nullptr;
	bool m_runningSimulation = false;
	Quackle::Game *m_game;
	MacondoCommand m_command = MacondoCommand::None;
};

#endif
