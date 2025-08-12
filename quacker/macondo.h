#ifndef MACONDO_H
#define MACONDO_H

#include <QWidget>

class QPushButton;
class QTimer;
namespace Quackle {
	class Game;
	class MoveList;
}
class MacondoBackend;
struct MacondoInitOptions;
class MoveBox;
class Macondo : public QWidget {
Q_OBJECT
public:
	Macondo(Quackle::Game *);
	~Macondo();
	void setGame(Quackle::Game *);
public slots:
	void simulate();
private slots:
	void gotSimMoves(const Quackle::MoveList &moves);
private:
	enum class Command {
		None,
		Simulate,
		Solve,
	};
	QPushButton *m_simulateButton;
	Quackle::Game *m_game;
	MacondoBackend *m_backend;
	int m_viewingPlyNumber = 0;
	Command m_command = Command::None;
	MoveBox *m_moveBox;
	std::unique_ptr<MacondoInitOptions> initOptions;
};

#endif
