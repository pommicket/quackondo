#include <QProcess>
#include <QWidget>

#include "view.h"


class QPushButton;
class QTimer;
class TopLevel;

class Macondo : public View {
Q_OBJECT
public:
	Macondo(TopLevel *topLevel);
	~Macondo();
public slots:
	void run();
	void updateResults();
	void processStarted();
	void processFinished(int, QProcess::ExitStatus);
	virtual void positionChanged(const Quackle::GamePosition *position);
private:
	void loadGCG();
	void killProcess();
	TopLevel *m_topLevel;
	QPushButton *m_runButton;
	QTimer *m_updateTimer;
	std::string m_execPath;
	QProcess *m_process = nullptr;
	int plyNumber = 0;
};
