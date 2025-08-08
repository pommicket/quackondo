#ifndef MACONDO_H
#define MACONDO_H

#include <QWidget>

class QPushButton;
class QTimer;
namespace Quackle {
	class Game;
	class Move;
}
class MacondoBackend;

class Macondo : public QWidget {
Q_OBJECT
public:
	Macondo(Quackle::Game *);
public slots:
	void simulate();
private slots:
	void gotSimMoves(const std::vector<Quackle::Move> &moves);
private:
	enum class Command {
		None,
		Simulate,
		Solve,
	};
	QPushButton *m_simulateButton;
	MacondoBackend *m_backend;
	int m_viewingPlyNumber = 0;
	Command m_command = Command::None;
};

#endif
