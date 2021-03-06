#pragma once

// std
#include <unordered_map>
#include <unordered_set>
#include <vector>

// opencv
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

// project
#include "kruskal.hpp"


namespace ppa {

	constexpr int RIDGE_FEATURES = 1;
	constexpr int VALLEY_FEATURES = -1;

	struct FeatureNode {
		int id = -1;
		cv::Vec2f p;
		std::vector<int> edges;
		FeatureNode() {}
		FeatureNode(int id_, cv::Vec2f p_) : id(id_), p(p_) { }
	};

	struct FeatureEdge {
		int id = -1;
		int node_start = -1, node_end = -1;
		std::vector<cv::Vec2f> path;
		FeatureEdge() {}
		FeatureEdge(int id_, int ns, int ne, std::vector<cv::Vec2f> path_ = {})
			: id(id_), node_start(ns), node_end(ne), path(path_) { }
		int other(int n) const{ return (n == node_start) ? node_end : node_start; }
	};

	class FeatureGraph {
	private:

		std::unordered_map<int, FeatureNode> m_nodes;
		std::unordered_map<int, FeatureEdge> m_edges;

		// helper struct
		struct edge {
			// required by kruskals implementation of edge_traits
			float weight;
			int id1, id2;
			cv::Point p1, p2;

			int other(int id) const { return (id == id1) ? id2 : id1; }
			cv::Point point(int id) const { return (id == id1) ? p1 : p2; }
		};

	public:

		int feature_type;

		FeatureGraph(const cv::Mat input, int grid_spacing = 10, int profile_length = 7, int feature_type_ = RIDGE_FEATURES) : feature_type(feature_type_) {
			assert(!input.empty());
			assert(input.type() == CV_32FC1);
			assert(grid_spacing >= 1);
			assert(profile_length >= 3);
			assert(std::abs(feature_type_) == 1);

			using namespace cv;
			using namespace std;

			// reduce down to operational grid
			Mat grid;
			resize(input, grid, input.size() / grid_spacing, 0, 0, INTER_NEAREST);
			grid *= feature_type;

			// inside region
			Rect grid_rect(Point(0, 0), grid.size());

			// work out the threshold for comparison
			double mmin, mmax;
			minMaxIdx(input, &mmin, &mmax);
			double thresh = 0.01*(mmax - mmin);

			int nodeidcounter = 0;
			Mat nodeids(grid.rows, grid.cols, CV_32SC1, Scalar(-1));

			// debug
			Mat debug_nodeids, input_int;
			input.convertTo(input_int, CV_8UC1);
			cvtColor(input_int, debug_nodeids, COLOR_GRAY2BGR);
			Mat debug_edges = debug_nodeids.clone();
			Mat debug_brokenedges = debug_nodeids.clone();
			Mat debug_reduceedges = debug_nodeids.clone();
			Mat debug_smoothedges = debug_nodeids.clone();
			Mat debug_ppa = debug_nodeids.clone();


			// Foward neighbours
			Point fneighbours[] = {
				Point(1, 0),
				Point(0, 1),
				Point(1, 1),
				Point(-1, 1)
			};


			// select feature points
			//
			for (int i = 0; i < grid.rows; i++) {
				for (int j = 0; j < grid.cols; j++) {
					Point p(j, i);
					float e = grid.at<float>(p);

					// neighbours
					for (Point n : fneighbours) {

						// for different profile lengths
						bool profile0 = false, profile1 = false;
						for (int l = 1; l <= profile_length / 2; l++) {
							Point delta = n * l;
							profile0 |= grid_rect.contains(p + delta) && e - grid.at<float>(p + delta) > thresh;
							profile1 |= grid_rect.contains(p - delta) && e - grid.at<float>(p - delta) > thresh;
						}

						if (profile0 && profile1) {
							nodeids.at<int>(p) = nodeidcounter++;

							circle(debug_nodeids, p * grid_spacing, 2, Scalar(0, 0, 255)); // debug

							break;
						}
					}
				}
			}


			// create graph
			//
			vector<edge> tempedges;
			for (int i = 0; i < grid.rows - 1; i++) {
				for (int j = 0; j < grid.cols - 1; j++) {
					Point p(j, i);
					float pe = grid.at<float>(p);
					int pid = nodeids.at<int>(p);
					if (pid < 0) continue;
					for (const Point &n : fneighbours) {
						Point q = p + n;
						if (grid_rect.contains(q) && nodeids.at<int>(q) >= 0) {
							float qe = grid.at<float>(q);
							// create an edge
							tempedges.push_back(edge{ pe + qe, pid, nodeids.at<int>(q), p, q });

							line(debug_edges, p * grid_spacing, q * grid_spacing, Scalar(0, 0, 255)); // debug
						}
					}
				}
			}


			// break cycles
			//
			tempedges = kruskal::minSpanForest(tempedges);

			for (const edge &e : tempedges) // debug
				line(debug_brokenedges, e.p1 * grid_spacing, e.p2 * grid_spacing, Scalar(0, 0, 255)); // debug


			// reduce graph
			//
			for (int i = 0; i < profile_length / 2; i++) {
				Mat degree(grid.rows, grid.cols, CV_32SC1, Scalar(0));
				for (const edge &e : tempedges) {
					degree.at<int>(e.p1)++;
					degree.at<int>(e.p2)++;
				}
				vector<edge> newedges;
				for (const edge &e : tempedges) {
					if (degree.at<int>(e.p1) > 1 && degree.at<int>(e.p2) > 1)
						newedges.push_back(e);
				}
				tempedges = newedges;
			}

			for (const edge &e : tempedges) // debug
				line(debug_reduceedges, e.p1 * grid_spacing, e.p2 * grid_spacing, Scalar(0, 0, 255)); // debug


			// smooth positions
			//
			unordered_map<int, Point> nodetoposition;
			unordered_map<int, vector<edge>> nodetoedge;
			for (const edge &e : tempedges) { // node to edge, and node to positions
				nodetoedge[e.id1].push_back(e);
				nodetoedge[e.id2].push_back(e);
				nodetoposition[e.id1] = e.p1;
				nodetoposition[e.id2] = e.p2;
			}

			unordered_map<int, Vec2f> smoothPosition;
			for (auto &nodep : nodetoposition) {
				Point p = nodep.second;
				float w = grid.at<float>(p);
				// the 1.01 here is to weight the original value slightly
				// so that neighbouring degree=1 points don't overlap (hack)
				float weight = 1.01 * w;
				Vec2f position = weight * Vec2f(p.x, p.y) * grid_spacing;

				for (auto e : nodetoedge.at(nodep.first)) {
					p = nodetoposition.at(e.other(nodep.first));
					w = grid.at<float>(p);
					weight += w;
					position += w * Vec2f(p.x, p.y) * grid_spacing;
				}

				smoothPosition[nodep.first] = position / weight;
			}


			// convert from edges to node/path
			//
			unordered_set<int> visited;
			int edgeidcounter = 0;
			for (const auto &nte : nodetoedge) {

				// find an end-node in the forest to start from
				int pid = nte.first;
				if (nte.second.size() == 1 && visited.find(pid) == visited.end()) {

					// perform depth first traversal from this node
					vector<int> toProcess;
					toProcess.push_back(pid);
					visited.insert(pid);
					m_nodes[pid] = FeatureNode(pid, smoothPosition.at(pid));

					// until tree is empty
					while (!toProcess.empty()) {
						int currentid = toProcess.back();
						toProcess.pop_back();

						// process edges (adding edges to the currentid node)
						for (const edge &e : nodetoedge.at(currentid)) {
							int next = e.other(currentid);
							if (visited.find(next) != visited.end()) continue;
							visited.insert(next);

							// begin constructing edge
							vector<Vec2f> path;
							path.push_back(smoothPosition.at(currentid));

							// while this edge leads to a path node
							while (nodetoedge.at(next).size() == 2) {
								// simply add to the path
								path.push_back(smoothPosition.at(next));
								// move the next node along the path
								int alongpath = nodetoedge.at(next)[0].other(next);
								if (visited.find(alongpath) == visited.end()) next = alongpath;
								else next = nodetoedge.at(next)[1].other(next);
								visited.insert(next);
							}

							// construct node
							m_nodes[next] = FeatureNode(next, smoothPosition.at(next));

							// finish constructing edge
							path.push_back(m_nodes[next].p);
							FeatureEdge fe(edgeidcounter++, currentid, next, path);
							m_nodes[currentid].edges.push_back(fe.id);
							m_nodes[next].edges.push_back(fe.id);
							m_edges[fe.id] = fe;


							// finally add this end-node or branch-node to process
							toProcess.push_back(next);
						}
					}
				}
			}


			// debug
			for (const auto &n : m_nodes) {
				circle(debug_ppa, Point(n.second.p), 3, Scalar(0, 0, 225));
			}
			for (const auto &e : m_edges) {
				Point p = e.second.path[0];
				for (int i = 1; i < e.second.path.size(); i++) {
					Point next = e.second.path[i];
					line(debug_ppa, p, next, Scalar(0, 255, 0));
					p = next;
				}
			}


			//debug
			imwrite("output/nodeids.png", debug_nodeids);
			imwrite("output/edges.png", debug_edges);
			imwrite("output/brokenedges.png", debug_brokenedges);
			imwrite("output/reduceedges.png", debug_reduceedges); 
			imwrite("output/smoothedges.png", debug_smoothedges);
			imwrite("output/ppa.png", debug_ppa);

		}

		const std::unordered_map<int, FeatureNode> & nodes() const { return m_nodes; }
		const std::unordered_map<int, FeatureEdge> & edges() const { return m_edges; }

	};

}