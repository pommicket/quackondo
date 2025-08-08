#include <QWidget>
#include "game.h"

class QPushButton;
class QProcess;
class QTimer;
class TopLevel;

class Macondo : public QWidget {
Q_OBJECT
public:
	Macondo(TopLevel *topLevel);
public slots:
	void run();
	void updateResults();
	void processStarted();
	void movesUpdated(Quackle::MoveList *moves);
	inline void setGame(Quackle::Game *game) { m_game = game; }
private:
	TopLevel *m_topLevel;
	QPushButton *m_runButton;
	QTimer *m_updateTimer;
	std::string m_execPath;
	QProcess *m_process = nullptr;
	Quackle::Game *m_game = nullptr;
};
