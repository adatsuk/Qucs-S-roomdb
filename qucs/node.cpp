/***************************************************************************
                          node.cpp  -  description
                             -------------------
    begin                : Sat Sep 20 2003
    copyright            : (C) 2003 by Michael Margraf
    email                : michael.margraf@alumni.tu-berlin.de
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/
#include "node.h"

#include "component.h"
#include "wire.h"
#include <unordered_set> // Needed to propagate the color property to wires and other nodes

#include <QPainter>

Node::Node(int x, int y)
  : DType("")
      , State(0), m_color(Qt::darkBlue), m_lineWidth(2)
{
  Type  = isNode;

  cx = x;
  cy = y;
}

void Node::paint(QPainter* painter) const {
  painter->save();

  if (isSelected) {
      painter->setPen(QPen(Qt::darkGray, 5));
      painter->drawEllipse(cx-5, cy-5, 10, 10);
  }
  else if (conn_count() == 1) {
      if (hasLabel()) {
        painter->fillRect(cx-2, cy-2, 4, 4, m_color); // open but labeled
      } else {
        painter->setPen(QPen(Qt::red, m_lineWidth));  // node is open
        painter->drawEllipse(cx-4, cy-4, 8, 8);
      }
  }
  else if (conn_count() > 2) {
      painter->setBrush(m_color);  // more than 2 connections
      painter->setPen(QPen(m_color, m_lineWidth));
      painter->drawEllipse(cx-3, cy-3, 6, 6);
  }
  else if (m_wires.size() != 2) {
      painter->fillRect(cx-2, cy-2, 4, 4, m_color);
  }

  painter->restore();
}

bool Node::getSelected(int x, int y)
{
  return cx - 3 <= x && x <= cx + 3 && cy - 3 <= y && y <= cy + 3;
}

void Node::setName(const QString& name, const QString& value, int x, int y)
{
  // Passing two empty strings acted like a signal to remove the label
  // and later was superseded by dropLabel() method. This assertion is
  // just merely a guard against legacy usage, it may be freely removed
  // after some time.
  // Added on 2025-06-12.
  assert(!(name.isEmpty() && value.isEmpty()));

  if (!hasLabel()) {
    acquireLabel(std::make_unique<WireLabel>(name, cx, cy, x, y));
  }
  else {
    label()->setName(name);
  }
  label()->initValue = value;
}

bool Node::moveCenter(int dx, int dy) noexcept
{
  Element::moveCenter(dx, dy);
  if (hasLabel()) {
    label()->moveRoot(dx, dy);
  }
  return dx != 0 || dy != 0;
}

  Node* Node::merge(Node* donor)
  {
    std::ranges::for_each(donor->wires(), [this,donor](auto* w) { w->Port1 == donor ? w->Port1 = this : w->Port2 = this; });
    std::ranges::copy(donor->wires(), std::back_inserter(m_wires));
    donor->m_wires.clear();

    for (auto* c : donor->components()) {
        for (auto* p : std::as_const(c->Ports)) {
            if (p->Connection == donor) {
                p->Connection = this;
            }
        }
    }

    std::ranges::copy(donor->components(), std::back_inserter(m_components));
    donor->m_components.clear();

    if (!this->hasLabel() && donor->hasLabel()) {
        this->acquireLabel(donor->releaseLabel());
    }

    this->isSelected = this->isSelected || donor->isSelected;

    return donor;
}

bool Node::isOverlapping(int otherX, int otherY) const {
  return (otherX == x() && otherY == y());
}

bool Node::isOverlapping(const Node* other) const {
  // Comparison of self is false
  if (this == other) {
    return false;
  }

  return isOverlapping(other->x(), other->y());
}


void Node::propagateStyle(const QColor& c, int lineWidth,
                          const std::list<Node*>& allNodes,
                          const std::list<Wire*>& allWires)
{
  std::unordered_set<Node*> visitedNodes;
  std::unordered_set<Wire*> visitedWires;
  std::list<Node*> nodeQueue;
  std::list<Wire*> wireQueue;

  // Set the color and queue a node
  auto visitNode = [&](Node* n) {
    if (visitedNodes.insert(n).second) {
      n->setColor(c);
      n->setLineWidth(lineWidth);
      nodeQueue.push_back(n);
    }
  };

  // Set the color and queue a wire
  auto visitWire = [&](Wire* w) {
    if (visitedWires.insert(w).second) {
      w->setColor(c);
      w->setLineWidth(lineWidth);
      wireQueue.push_back(w);
    }
  };

  visitNode(this);

  // Drain the nodes and wires queues. A visit to a node may find new nodes to queue. The same with wires.
  while (!nodeQueue.empty() || !wireQueue.empty()) {
    // Inspect nodes
    while (!nodeQueue.empty()) {
      Node* node = nodeQueue.front();
      nodeQueue.pop_front();

      // Propagate through every wire touching this node.
      for (Wire* wire : node->wires()) {
        visitWire(wire);
      }

      // Look for any other node sharing this node's net name.
      if (node->hasLabel()) {
        const QString key = node->label()->Name;

        // Nodes
        for (Node* candidate : allNodes) {
          if (candidate != node && candidate->hasLabel() && candidate->label()->Name == key) {
            visitNode(candidate);
          }
        }
        for (Wire* candidate : allWires) {
          if (candidate->hasLabel() && candidate->label()->Name == key) {
            visitWire(candidate);
          }
        }
      }
    }

    // Inspect wires
    while (!wireQueue.empty()) {
      Wire* wire = wireQueue.front();
      wireQueue.pop_front();

      // Propagate to the nodes at the ends
      if (wire->Port1) {
        visitNode(wire->Port1);
      }
      if (wire->Port2) {
        visitNode(wire->Port2);
      }

      // Look for any other wire/node sharing this wire's net name.
      if (wire->hasLabel()) {
        const QString key = wire->label()->Name;

        // Wires
        for (Wire* candidate : allWires) {
          if (candidate != wire && candidate->hasLabel() && candidate->label()->Name == key) {
            visitWire(candidate);
          }
        }

        // Nodes
        for (Node* candidate : allNodes) {
          if (candidate->hasLabel() && candidate->label()->Name == key) {
            visitNode(candidate);
          }
        }
      }
    }
  }
}

void Node::connect(Wire* wire)
{
  if (is_connected(wire)) return;
  m_wires.emplace_front(wire);

  if (wire->color() != Qt::darkBlue && m_color == Qt::darkBlue) {
    m_color = wire->color();
    m_lineWidth = wire->lineWidth();
  } else if (m_color != Qt::darkBlue && wire->color() == Qt::darkBlue) {
    wire->setColor(m_color);
    wire->setLineWidth(m_lineWidth);
  }
}
