#include <QWidget>

class QPushButton;
class QTimer;
namespace Quackle {
	class Game;
}
class MacondoBackend;

class Macondo : public QWidget {
Q_OBJECT
public:
	Macondo(Quackle::Game *);
public slots:
	void simulate();
	void updateResults();
private:
	enum class Command {
		None,
		Simulate,
		Solve,
	};
	QPushButton *m_simulateButton;
	QTimer *m_updateTimer;
	MacondoBackend *m_backend;
	int m_viewingPlyNumber = 0;
	Command m_command = Command::None;
};
