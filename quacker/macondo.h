#ifndef MACONDO_H
#define MACONDO_H

#include "view.h"
#include "game.h"

class QCheckBox;
class QTimer;
namespace Quackle {
	class Game;
	class MoveList;
}
class MacondoBackend;
struct MacondoInitOptions;
class MoveBox;
class Macondo : public View {
Q_OBJECT
public:
	Macondo(Quackle::Game *, MoveBox *);
	~Macondo();
	void setGame(Quackle::Game *);
	// stop current analysis
	void stop();
	// should Macondo be used for simulations?
	bool useForSimulation() const;
signals:
	void newMoves(const Quackle::MoveList *);
public slots:
	void simulate();
private:
	QCheckBox *m_useMacondo;
	Quackle::Game *m_game;
	MacondoBackend *m_backend;
	Quackle::MoveList m_moves;
	int m_viewingPlyNumber = 0;
	MoveBox *m_moveBox;
	std::unique_ptr<MacondoInitOptions> initOptions;
};

#endif
