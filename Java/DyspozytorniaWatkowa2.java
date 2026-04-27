import java.util.LinkedList;
import java.util.Set;
import java.util.Deque;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;

public class DyspozytorniaWatkowa2 implements Dyspozytornia {

    private Map<Integer, Taxi> taksowki = new HashMap<>();
    private Deque<Integer> wolne = new LinkedList<>();
    private Deque<Integer> zlecenia = new LinkedList<>();
    private AtomicInteger nastepnyNumerZlecenia = new AtomicInteger(0);
    private AtomicInteger zepsuta = new AtomicInteger(-1);
    private AtomicBoolean running = new AtomicBoolean(true);
    private Object semafor = new Object();

    Thread wykonywaczZlecen = new Thread(new Runnable() {
        public void run() {
            while (running.get()) {
                if (zlecenia.isEmpty() || wolne.isEmpty()) {
                    try {
                        if(!running.get()) break;
                        this.wait();
                    } catch (InterruptedException e) {
                        continue;
                    }
                }

                synchronized (semafor) {
                    if (!zlecenia.isEmpty() && !wolne.isEmpty()) {
                        int wolnaID = wolne.poll();
                        int zlecenieID = zlecenia.poll();
                        Taxi wolna = taksowki.get(wolnaID);
                        Thread th = new Thread(new Runnable() {
                            public void run() {
                                int id = wolnaID;
                                int zlecenie = zlecenieID;

                                try {
                                    wolna.wykonajZlecenie(zlecenie);
                                } catch (Exception e) {

                                } finally {
                                    synchronized (semafor) {
                                        if (zepsuta.get() != id) {
                                            wolne.add(id);
                                        }
                                        semafor.notifyAll();
                                    }
                                }
                            }
                        });
                        th.setDaemon(true);
                        th.start();
                    }
                }
            }
        }
    });

    public DyspozytorniaWatkowa2() {
        wykonywaczZlecen.setDaemon(true);
        wykonywaczZlecen.start();
    }

    public void flota(Set<Taxi> flota) {
        for (Taxi taxi : flota) {
            int numer = taxi.numer();
            taksowki.put(numer, taxi);
            wolne.add(numer);
        }
    }

    public int zlecenie() {
        int numerZlecenia = nastepnyNumerZlecenia.incrementAndGet();
        Thread th = new Thread(new Runnable() {
            public void run() {
                synchronized (semafor) {
                    zlecenia.add(numerZlecenia);
                    semafor.notifyAll();
                }
            }
        });
        th.setDaemon(true);
        th.start();
        return numerZlecenia;
    }

    public void awaria(int numer, int numerZlecenia) {
        synchronized (semafor) {
            zepsuta.set(numer);
            zlecenia.addFirst(numerZlecenia);
            semafor.notifyAll();
        }
    }

    public void naprawiono(int numer) {
        synchronized (semafor) {
            wolne.add(numer);
            semafor.notifyAll();
        }
    }

    public Set<Integer> koniecPracy() {
        running.set(false);
        semafor.notifyAll();
        Set<Integer> out = new HashSet<>(zlecenia);
        return new HashSet<Integer>(out);
    }
}