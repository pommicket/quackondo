#include <QWidget>

class QPushButton;
class QProcess;
class QTimer;

class Macondo : public QWidget {
Q_OBJECT
public:
	explicit Macondo(QWidget *parent = 0);
public slots:
	void run();
	void updateResults();
	void processStarted();
private:
	QPushButton *m_runButton;
	QTimer *m_updateTimer;
	std::string m_execPath;
	QProcess *m_process = nullptr;
};
